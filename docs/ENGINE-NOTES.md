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

*This paragraph predicted the price. §7al records what it actually was once
the design was done: the split is forced in exactly two places, neither of them
the RHI, and the rest stays common.*

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

> **The colour, and at the time only the colour.** The depth attachment
> was deliberately left multisampled and unresolved, with a comment in
> both backends saying nothing downstream of the scene pass reads it.
> That was true when this was written and stopped being true four times
> over — 9.4, 9.5, 9.6 and 9.7 each reconstruct a position from the scene
> depth. **§7ai** is what closes it, and what the phase between cost.

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

## 7ac. SSAO (9.6): occlusion from depth alone, and what "alone" costs

Two design questions, both settled before code.

### Normals: reconstructed from depth, no new attachment

Ambient occlusion needs a surface normal to orient its hemisphere. The scene
target does not carry normals, so either it grows an attachment or the pass
reconstructs them from depth. **Reconstruction wins here**, and the reasons
are worth recording because 9.7 may reverse the answer for itself:

- A new attachment is the full 7q ceremony: every scene pipeline, the
  transparency pass, scenetest's own call sites and *both* probe capture
  paths have to agree about the target's shape, and the record of how that
  goes is a pattern that "bit three times in a row" in one day.
- AO is low frequency and half resolution; reconstruction artefacts live at
  depth discontinuities, exactly where the blur is already depth-aware.
- The reconstruction is not the naive cross of derivatives, which invents a
  45-degree normal along every silhouette. Each axis differences toward
  whichever neighbour is *closer in depth* -- four taps, pick two -- so an
  edge pixel takes its slope from its own surface rather than from the gap.

SSR (9.7) wants normal-mapped normals and roughness, which no reconstruction
can produce; if it adds a G-buffer-lite attachment, SSAO can switch to
reading it in the same commit. Until something needs that attachment for
real, nothing pays for it. (7ad added it; 7ae made the switch, and the
reconstruction described here is now the fallback for texels the scene did
not write.)

### Applied as a multiply on the lit image, which is a stated compromise

The honest place for AO is inside the lighting integral, attenuating ambient
and image-based light only. A single forward pass cannot have it there: the
occlusion is computed *from* the depth that pass writes. So it applies as a
multiply on the linear HDR image after the resolve -- which darkens direct
light too, the compromise every forward-plus-post AO ships. It is kept
honest by the intensity dial defaulting low and by the design treating AO as
contact shadowing rather than as global illumination. Before DoF and motion
blur, because occlusion is lighting and belongs on the sharp image the
temporal filter resolved, not smeared over afterwards.

### The passes, and the two floats FrameDesc grows

Half-resolution compute, two depth-aware separable blur passes, and a
full-resolution apply -- the same half-res economics as bloom and DoF, for
the same reason. The compute reconstructs view-space position as
`(ndc.xy * InvProjection, 1) * linearDepth`, which needs the projection's
two diagonal scales; `FrameDesc` carries their inverses, filled by every
caller that owns a camera. Only *relative* positions matter to occlusion, so
the backend's NDC y-direction needs no reconciliation beyond the sampling
FlipY every post pass already owes. Perspective is assumed: an orthographic
camera gets a slightly wrong radius falloff and no crash, and the day a 2D
project cares is the day this grows a branch.

The kernel is the DoF gather's golden-angle spiral bent into a hemisphere,
rotated per pixel by the grain's integer hash of the **pixel coordinate
alone** -- the same determinism sentence as 7ab, and it will stay in every
one of these sections: seeded per frame it sparkles under TAA and costs
--screenshot-frame its meaning.

### The check is about restraint, not darkness

Any SSAO darkens a corner. The failure that ships is the other half:
darkening what is *open* -- self-occlusion speckle across flat floors, halos
around silhouettes. So the fixture is a box on a floor and the check
measures both regions: the contact seam must darken, the open floor must
not (within a tolerance that a biased kernel fails), intensity must scale
the first monotonically while leaving the second alone, off is off to the
byte, and a frame reproduces. ENGINE-NOTES 7ab's fixture pattern, pointed
at restraint.

---

## 7ad. SSR (9.7): the first new attachment since velocity, and a march

The probes answer "what does this surface reflect" from one point in the
room; SSR answers it from the surface's own point of view for anything
already on screen. Both are needed: SSR alone has nothing to say about what
is behind the camera or off the edge, and probes alone put the plinth's view
of the courtyard on the floor under a crate. So the shape is **trace, and
fall back to the probe wherever the trace has no answer** -- one blend, not
two systems.

### The attachment, finally

7ac deferred this and said SSR would force it. It does. A reflection needs
the surface's real normal -- normal-mapped, per pixel, which the depth
reconstruction cannot know -- and its roughness, or every floor is a mirror.
Neither is in the scene target, so the target grows a fourth colour
attachment: **RGBA8, octahedral-encoded normal in RG, roughness in B,
metallic in A.** Eight bits per octahedral component is ~1 degree of
precision, ample for a reflection direction; the packing is what lets one
cheap attachment carry the whole surface description.

It costs the 7q ceremony in full, and this section is the record of the
sweep so the next attachment does not rediscover the list. The format is
stated in `Renderer::SetTargetFormats` and fans out to every renderer that
draws into the scene target -- Renderer2D, Renderer3D, DebugRenderer,
ParticleRenderer, Skybox, ViewportGrid, and the UI world layer through its
own setter -- each of which declares it in its pipeline `ColorFormats`
after the velocity, exactly as they declare the velocity. It is stated
again at layer init in the editor and the runtime, and in scenetest's two
call sites, and in `ReflectionProbe`'s face target, because a probe captures
the scene before the graph is built (Cold start's "bit three times in a
row"). The scene pass writes it with a clear of zero, which decodes to a
degenerate normal and roughness 0 -- and the resolve treats "no surface" as
"no reflection", so anything that never writes the attachment (sky, grid,
particles, text) simply does not reflect. Only the PBR shaders write real
values.

SSAO could now read the real normal instead of reconstructing one. It
did not, that day: the reconstruction was measured to hold a flat floor at
zero drift, and swapping a working input for a different working input on
the same day as the attachment lands is two changes in one commit. It was
the first follow-up, and it is 7ae -- which also records what the swap
found in this section's own normal transform.

### The trace

Half resolution, like every gather since bloom. Per pixel: reconstruct the
view position (7ac's arithmetic), decode the normal, reflect the view ray,
and **march the reflected ray in view space against the depth buffer** --
a fixed number of linear steps, then a binary refinement between the last
miss and the first hit. Linear rather than hierarchical: hi-Z is a real
optimisation and it is a later one; at 24 steps over a bounded distance the
linear march costs less than the DoF gather and finds everything a demo
courtyard has to find. A step counts as a hit only if the scene depth is in
front of the ray *by less than a thickness*: without the thickness bound a
ray passing behind a thin railing hits the wall behind it, and with an
unbounded one it hits the railing from a metre away.

The trace writes **hit uv + confidence**, not colour. Confidence folds in
the edge fade (a hit near the screen edge fades toward the probe, so a ray
leaving the screen produces no seam), the ray-length fade, and a facing
fade for rays reflecting back toward the camera, whose hits are what is
*behind* the surface in the depth buffer and are always wrong. Writing the
uv rather than the colour is what lets the resolve pass sample the lit
image at full resolution and blur it by roughness -- a rough surface's
reflection is the mirror hit blurred, and blurring the *hit colour* over a
disc sized by roughness is a defensible approximation of the GGX lobe that
a single mirror ray cannot express. Multiple jittered rays would be more
correct and are the follow-up after hi-Z.

### The blend, and where -- as it was, superseded by 7af

The trace should *replace* the probe's contribution where it is confident,
not add a second reflection on top of the one the PBR shader already
composited. Doing that exactly means reproducing the PBR shader's probe
term -- `prefiltered(slot, lod) * (F0 * envBRDF.x + envBRDF.y) * occlusion`
-- which needs the per-instance probe slot, the material occlusion and F0,
none of which the attachment carries. So v1 approximated: the resolve
computed the specular weight from the attachment's metallic (F0 =
mix(0.04, ~albedo, metallic), with the albedo taken as the lit pixel
itself), scaled it by confidence and by `(1 - roughness)`, and wrote
`lit * (1 - w) + hit * w`. It was written down as an approximation, and it
was one: on the mirror-floor fixture its weight was off by a factor of
three and a half at grazing incidence, and on a metal sphere reflecting a
dark scene it took the sphere's albedo from the sphere's darkness. **9.9
(7af) removed the approximation by moving the blend out of the post chain
and into the lighting** -- the PBR shader now swaps `prefiltered` for the
traced radiance under the exact weight, one frame late, reprojected. This
section stays as the record of the first design and of why it could not be
exact from where it stood.

Off is exact: no pass, and the attachment is written either way -- its
presence is a *shape* decision, not a setting.

### The check

A box on a mirror floor. The floor region below the box's base must gain
the box's colour with SSR on; a rough floor must gain less of it; off is
off to the byte; a frame reproduces (the resolve's roughness disc is a
fixed pattern, no dither at all in v1); and both backends agree, which is
where a wrong Y convention in the reflected uv would show first.

---

## 7ae. The reconstruction frame, stated once (9.8): SSAO's real normal, and the transform it exposed

The first 9.x follow-up: SSAO stops reconstructing its normal from depth
and reads the one the scene wrote into 7ad's attachment. Small on paper.
Doing it meant SSAO needed the same "world normal into the frame the pass
reconstructs" transform the SSR trace already had, and writing that
transform down *once* for both passes meant deriving it rather than copying
it -- and the derivation did not match the copy.

### The frame

Every depth-reading post pass turns a sampling-space uv and a depth back
into a point: `(ndc.xy * InvProjection * z, z)`. That is not the camera's
view space. Its x is the camera's; its **y runs along increasing texel
rows**, because the uv is the one the depth was sampled with -- up the
picture on the backend whose row 0 is the bottom (OpenGL), down it on the
one whose row 0 is the top (Vulkan), the same fact every post pass's FlipY
exists for; and its **z is the linear depth, positive in front**, where view
space looks down -z. So the frame is view space under a fixed orthogonal
map per backend: `diag(1, 1, -1)` on OpenGL, `diag(1, -1, -1)` on Vulkan.
Positions and directions computed inside it agree with each other by
construction -- ViewToUv is the exact inverse of ViewPosition. The one thing
that arrives from outside is the normal, in world space, and it has to come
in through the same map: view rotation, then negate z, then negate y on the
flipped backend. `include/view_reconstruction.glsl` is that map, and SSAO
and the SSR trace both include it; neither carries a private copy.

### What the copy had been

7ad's trace negated y on the *unflipped* backend and nothing on the flipped
one, and had "measured, not argued" that on a mirror floor. The
measurement was honest and the transform was wrong, and both are true
because of one property of `reflect()`: it cannot tell a normal from its
negation. Compare the code's `(x, -y, z)` with the correct `(x, y, -z)` on
OpenGL: they are negations of each other exactly when `x = 0`. A floor seen
without roll, a wall facing the camera, a wall facing sideways -- every
surface in every fixture and in the demo courtyard -- has a view-space
normal with no x-component, and reflected identically under both. A mirror
sphere does not: its normals point every way, and under the old transform
the slab standing to its left reflected on its *right* limb. That sphere is
now in `check_ssr.py`, and the old transform fails it on both backends
(left 0.9, right 1.7 levels) where the new one passes (left 15.4, right
0.0). The floor case is unchanged to the second decimal, which is the
whole point: the floor could never have told.

The lesson is not "measure more". It is that a transform between two
frames is a *derivation* -- state both frames, write the map -- and a
measurement confirms a derivation rather than replacing one. Where the
derivation is skipped, the measurement has to be chosen to break the
symmetry the code might have, and nobody knows what symmetry that is until
the derivation is done.

### SSAO's switch, and the rule it needed the same day

SSAO reads the attachment (point sampled, like the trace) and takes its
normal **where the texel is a surface and the written normal agrees with
the geometric one** -- within 16 degrees, a few times the reconstruction's
wobble on a distant floor and under any normal map worth authoring; the
chosen-neighbour reconstruction from 7ac stays as the answer everywhere
else. Empty texels -- sky, the editor grid, particles, text -- keep the
reconstruction because the grid, in the editor, receives contact shadows
from the objects standing on it and should go on doing so.

The agreement condition was not the first version. The first took the
written normal wherever it faced the camera, on the argument that AO
should "follow the mortar and not a flat plane" -- and the owner, looking
at the demo the same day, said the walls looked off. They did, and the
argument was wrong: **the occluders in a screen-space kernel are the depth
buffer, which knows geometry and nothing else.** A texel whose normal map
tilts it thirty degrees off a flat wall is still a flat wall to every
depth sample around it; a hemisphere tilted thirty degrees dips its lower
taps into that wall, and the wall occludes itself along every mortar line.
Where the shading normal disagrees with the depth buffer's own slope, the
disagreement is a bump the depth buffer does not have, and occlusion by
that buffer has to be measured against the normal that buffer implies.
Curved surfaces -- where the reconstruction is noisy and the written normal
is smooth, which was the point of the switch -- agree, and take the written
one. The same test refuses a shading normal that leans away from the camera
at a grazing angle, since it cannot agree with a geometric one that faces
it.

Measured on a fixture built for it -- the courtyard's brick material on a
wall seen along its length, nothing near it, lit by a flat sky, the AO
*factor* (on over off, because a difference scales with the bricks) over
the open wall: the geometric normal holds 1.000 with a worst texel of
0.984; the written normal wherever it faces the camera 0.997 with a worst
of 0.954, in the brick pattern; the agreement rule 1.000 and 0.984, both
backends. `check_ssao.py` has the wall now, with the bound between those
two. On the box-on-a-floor fixture the switch's original measurement
stands: the seam darkens 13.6 levels on both backends where the
reconstruction gave 12.1 on Vulkan and 13.3 on OpenGL, because the box
faces agree and take the written normal, which does not wobble with each
backend's depth quantisation. The open floor holds at zero drift as before.

The lesson sits beside 7ae's other one. "Follows the mortar" sounded like
fidelity; it was a category error about what the kernel measures, and the
demo showed it before the fixture did.

### The sentinel

"Is this texel a surface" was tested on the octahedral normal being
`(0, 0)`. That test is not safe: the four corners of the octahedral square
all decode to -z, and a normal pointing almost exactly along -z with the
faintest negative x and y quantises to the same `(0, 0)` the clear holds --
a wall facing that way would flicker between surface and nothing a texel at
a time. The PBR shader clamps roughness to 0.045 before writing it, so a
real surface never stores a zero in the roughness channel and the clear
always does. `SurfaceIsEmpty` tests that channel, and both passes call it.

---

## 7af. SSR replaces the probe exactly (9.9): the blend moves into the lighting, one frame late

7ad's resolve blended a guessed weight over the lit pixel because the
exact weight -- `(F0 * envBRDF.x + envBRDF.y) * occlusion`, times the
probe's radiance -- lives inside the PBR shader and nowhere else. The
9.x list said the fix was "one more channel in the attachment". It is not:
the weight is three channels of F0, an occlusion and the probe's radiance
in the reflected direction, six numbers, and the attachment has one free
8-bit slot. Two more attachments would carry them (7q's ceremony twice,
twelve bytes a pixel on every scene draw whether the feature is on or off,
and five colour attachments where the Vulkan floor is four). So the
question was turned around: **not "how does the post pass learn the
weight" but "how does the lighting learn the trace".**

### The shape

The trace is written at the end of a frame -- from that frame's depth,
surface attachment and lit image, after SSAO -- into one half of a
per-chain `TemporalHistory` pair, RGBA16F: RGB the radiance the ray found,
blurred by roughness, A the confidence. The *next* frame's scene pass binds
the other half at set 0 binding 12 and the PBR shader, at the one line where
it reads the probe's `prefiltered`, does

```
prefiltered = mix(prefiltered, traced.rgb, traced.a * intensity);
```

before the split-sum weight and the occlusion apply. Same weight, same
occlusion, same F0, same everything: the traced radiance goes through
exactly the arithmetic the probe's did. The post chain no longer touches
`shaded` for SSR at all; the resolve pass writes the pair and nothing else.
`Renderer::SetScreenReflections` hands the texture and intensity to the
scene pass and clears them after it, on the same edges as the jitter and
the camera motion and for the same reason -- a probe face or a shadow
caster reaching this would light itself from a trace made for another
camera. `FrameDesc::Reflections` is the pair, one per chain like `History`
and `Exposure`; a caller with none gets the probe alone and no SSR passes,
the same shape as TAA with no history.

The lookup is where the surface *was*: `v_PrevClipPos`, the same previous
clip position the motion vector is built from, less last frame's jitter,
mapped into the trace's row space by a per-backend sign the scene block
carries (`ScreenReflections.y`, -1 where row 0 is the top -- the fact
taa_resolve states as FlipY). Off the edge last frame means nothing was
traced for the point and the probe answers; clamp-to-edge would have
answered with a neighbour's reflection.

### What it costs, stated

**One frame of latency.** The reflection a surface shows is the trace of
the previous frame's image, reprojected through the surface's own motion.
For the reflector that is invisible: the reflection stays attached to the
floor as the camera pans. For the *reflected* content it is one frame
stale -- a ball rolling across a mirror at 1 m/s at 60 Hz reflects from
1.6 cm behind itself. Every shipping SSR traces last frame's colour or
reprojects a temporal accumulation; this is the smaller of the two debts.
Newly disoccluded pixels sample the trace made for whatever was there a
frame ago and are wrong for one frame; nothing rejects that yet, and if it
ever shows, the confidence channel is where a depth test would go.

**Multi-bounce, for free.** The trace samples the lit image, which already
contains last frame's reflections; a mirror facing a mirror shows the
mirror's reflection. Each bounce carries another factor of the weight, so
it converges.

**Nothing on the scene target.** The scene draws no more attachments than
before, and off costs one uniform branch. The trace and resolve are where
they were, minus a blend: SSR at 1440p on the demo is 0.46 ms on Vulkan
and 0.39 on OpenGL, unchanged from 7ad within the noise.

**Determinism holds.** A frame's SSR is the previous frame's trace, which
in a still scene is the same trace; `--screenshot-frame=30` reproduces to
the byte, and off is off to the byte because the shader's branch is on the
intensity and the texture bound when it is zero has zero alpha anyway.

### The check is a law, not a vibe

A metal's lighting is its specular term alone, and that term is the
reflected radiance times the weight. So a metal floor reflecting the block
under any sky, with SSR on, must equal the same floor with SSR *off* under
a uniform sky the colour of the block -- the weight is identical and only
the radiance was swapped. `check_ssr.py` renders both under a uniform sky
(the mirror fixture's procedural gradient with all three colours equal)
and takes the mean absolute difference over the interior of the block's
reflection: **0.00 levels on both backends**, at 208 of 255. 7ad's blend
could not have passed this at any tolerance; the mirror region that gained
23 levels under it gains 80 under the exact weight, and the sphere's limb
15 became 84. Those are not "more reflection" -- they are the amount a
dark metal at grazing incidence actually reflects, which the guess had
under by the Fresnel term it did not know.

### Two things the exact weight made visible, and what was done about them

With the reflection three times brighter, two artefacts of 7ad's resolve
that had been faint became plain.

**Filtering hit coordinates.** The resolve read the half-resolution trace
with a bilinear sampler and then sampled the lit image at the result. A
hit uv is a *place*; the average of a texel that hit and a texel that did
not is three quarters of the way to the corner, which is somewhere no ray
went -- and along every reflection's silhouette, and at the horizon of a
grazing floor, that somewhere was the block, at a quarter confidence: a
yellow dash on the far floor, on Vulkan only, because the two backends
straddle the half-resolution texels at different phases. The resolve now
point-samples the four nearest trace texels, resolves each at its own hit,
and blends the *radiances* by bilinear weight times confidence. Radiance
can be averaged; a place cannot. The dash is gone and the silhouettes are
clean; the cost did not move.

**Grazing self-hits.** At the far edge of a floor seen nearly edge-on, a
texel of depth spans a quarter of a metre, and a ray rising three
centimetres a step from a point sample of it reads as "behind the surface"
for its first few steps -- a hit on the floor it left, at the floor's own
dark colour, a two-row band a few levels off at the horizon on one
backend. This is the linear march comparing a ray position against a
point-sampled depth at a slightly different position; the fix is a march
that compares at texel centres, which is what a screen-space DDA does and
what hi-Z (9.10) is anyway. Written down here as the thing 9.10 must
remove; not patched with a bias that would trade thin-object hits for it.

### The binding

`u_ScreenReflections` is set 0 binding **12**, not 11. `pbr_fragment.glsl`
is included by the skinned variant, whose vertex stage keeps the bone
buffer at set 0 binding 11; one set, one binding, two descriptor types is a
pipeline that does not build, and the first attempt found that as a device
loss the moment the fox drew -- under validation, one line naming the
binding; without it, `VK_ERROR_DEVICE_LOST` from a fence wait three seconds
in. Both are in HANDOFF's verification bar for a reason.

### What 7ac deferred, this makes possible

7ac put SSAO on the lit image because "a single forward pass cannot have
it inside the lighting: the occlusion is computed from the depth that pass
writes." That sentence is now false in the same way it was for
reflections: the occlusion of frame N, reprojected through
`v_PrevClipPos`, could attenuate frame N+1's ambient and image-based
light *only*, which is the honest place 7ac named and could not reach.
Not built; noted, because the mechanism now exists and the section that
said it could not should say so.

---

## 7ag. The SSR march becomes a walk (9.10): screen space, a crossing, and a pyramid that helps less than it should

The 9.x list called this "hi-Z for the SSR march" and priced it as a
performance item. It turned into a correctness item first, because the
owner looked at the demo's brass sphere under 9.9's correct weight and saw
what 7ad's march did to a curved surface: orange flecks where a smooth
reflection should be. The march had three faults, all of them one fault --
**it compared the ray's depth against a depth sampled somewhere else.**

### What the linear march got wrong

7ad stepped a fixed distance in view space, projected each step to a uv,
and read the depth there. The read lands on a texel whose centre is up to
a texel away from where the ray is; on a floor seen at a graze a texel of
depth spans a quarter of a metre, and a ray rising three centimetres a
step reads as behind the floor it just left -- the two-row band at the
horizon 7af recorded. On a sphere the step was half a metre and the
thickness half a metre, so a ray leaving the limb passed behind the
sphere's own bulge within the thickness and hit it: the sphere reflected
itself, in flecks, because whether a given ray did depended on where its
steps happened to fall. And a hit was "behind by less than a thickness",
which couples the thickness to the step -- a thin object needs a large
thickness to be found and a large thickness finds things that are not
there.

### The walk

The march is in screen space now. Both ends of the ray project to the
trace's pixels; the ray's depth along the projected line is
`1 / mix(1/z0, 1/z1, t)`, exact under perspective, so at any pixel the
walk knows the ray's depth *at that pixel* and compares it with the depth
stored *for that pixel*. A hit is a **crossing**: in front of the depth
buffer at one sample, not in front at the next. That single rule removes
every self-hit -- the ray's own origin is on its surface, neither in front
nor behind, and a ray leaving a sphere's limb toward its bulge is behind
from the first sample, so it never crosses -- and it decouples the
thickness from the step. The thickness now asks one question, of the
sample *before* the crossing: how far behind the surface it is about to
meet was the ray already? A real surface is met from in front, so zero or
less; a railing the ray went past is met from a metre behind, having been
in front of the wall beyond, and is rejected. (Asked of the sample after
the crossing it rejects every steep ray, whose depth moves more than a
thickness in one stride; that was a bug on the way here.) The landing
point is solved analytically between the two samples that bracket it, so
a coarse stride costs precision only where the surface curves between
them.

The first version of the walk required "behind" at the crossing rather
than "not in front"; a ray that arrived at a wall without overshooting
read as *on* it, that reset the front, and half the block's reflection was
holes. Recorded because it is the kind of off-by-one a crossing test
invites.

### The pyramid, and what it is for

Above the level-0 walk sits a **min/max depth pyramid** -- six levels over
the trace's pixels, each cell holding the nearest and farthest depth
beneath it -- and the walk uses it the way hi-Z is meant to be used: a
ray whose depth range across a cell is nearer than everything in it skips
the cell and tries a coarser one; farther than everything in it, likewise
(if it was not just in front -- a ray that was in front and is now behind
everything crossed something at the cell's edge, and looks closer
instead); overlapping, it descends. The pyramid is two atlases built by
two passes, because the RHI has no per-mip render targets and reading a
target while writing it is a hazard on both backends: fine levels straight
from the scene depth, coarse levels from the fine atlas's last level.
Two passes rather than one because the first version built all six levels
in one pass and the *smallest* level's few hundred texels, each taking the
minimum of a thousand fetches in a loop the compiler cannot unroll, made
that pass cost 0.4 ms -- more than the march it existed to accelerate.
Two passes cap the block at 64 fetches and the build at 0.08 ms.
`include/hiz_atlas.glsl` is the layout, and both the builder and the walk
include it.

Now the honest part. **On the demo the pyramid does not pay.** The
courtyard's reflections come off a floor and three walls seen at a graze,
and a ray leaving a surface at a graze hugs it: its depth range across any
cell overlaps the cell's, at every level, so it can never use a coarse
cell and walks level 0 -- which is what the pyramid cannot help, and what
a screen-space DDA does at a stride. So level 0 *is* a DDA: a stride of
three texels, growing two percent a step, comparing at the texel; the
pyramid takes over only when a level-0 sample finds the ray clear of its
surface by several strides' worth of depth. Rays that leave -- toward the
sky, off a sphere, off a wall seen head-on -- climb and cross the frame in
a dozen iterations; rays that stay pay the DDA. Measured at 1440p on the
demo, the SSR total: 7ad's linear march 0.46 ms (wrong); the DDA alone
0.67; the walk with the pyramid 0.75, of which 0.13 is fixed and 0.08 the
pyramid. Iteration counts, imaged: the walls and floor at a graze run past
a hundred, everything else under forty. The pyramid earns its place on the
scenes where reflections leave their surfaces, and this section says
plainly that the demo is not one. A rough surface's budget scales down
with its roughness fade, since its reflection is a blur the trace only
has to place.

### What the check saw

`check_ssr.py`, unchanged in what it asks: mirror gain 79.6, rough 14.9,
exactness 0.00 levels, both backends 0.1 rows apart, off byte-exact, a
frame reproduces. The sphere's limb went from 84 to 104 levels -- more of
the limb resolves -- and its crescent, imaged, is solid where 7ad's was
ragged with a hole. The horizon band is gone: zero pixels differ between
on and off outside the block's reflection, on either backend. On the demo
the brass sphere reflects the wall and the flame as a continuous band.

What 9.10 does not do: multiple rays for a roughness lobe (the resolve's
disc still stands in), and any temporal reuse. Both are the follow-ups
after this one, and both would want the walk exactly as it is.

---

## 7ah. The probe convolution in six passes, not thirty-six (9.11)

V.3 measured the reflection-probes phase at 0.70 ms on Vulkan against 0.18
on OpenGL for the same work, and named the shape: the prefilter rendered
each of a probe's six faces at each of its levels into its own square
scratch target and copied it into the cube array on its own -- thirty-six
render passes and thirty-six copies per probe, every copy a pair of
full-image layout transitions on a cube *array*, and on Vulkan a small
pass between two barriers is a GPU idling for the pass's length. The
15 Hz probe dial hid it; this is the fix underneath.

**One pass per level, six faces across.** The scratch is a strip six faces
wide; the six faces are six viewports in one render pass, one resource set
between them, and the fullscreen triangle covers whatever the viewport is.
On Vulkan the viewport has to carry the negative height the pass's own
default viewport carries, or that backend's faces come out upside down
while the other's do not -- the same fact stated for the copy in 7g, met
from the render side. **One copy per level**:
`RHICommandList::CopyStripToTextureLayers` takes the strip and lands slice
i in layer `base + i` at the mip -- one pair of transitions and one blit
with six regions on Vulkan, six framebuffer blits on OpenGL, the same
per-slice flip the single-face copy does. Same shading, same rows: the
demo frame on Vulkan is byte-identical before and after.

**Measured**, probe updating every frame, 1440p, demo: Vulkan probes
phase 0.77 -> 0.59 ms; OpenGL 0.24 -> 0.23. A quarter off the Vulkan
cost, and Vulkan still two and a half times OpenGL -- what remains is the
six scene captures, which are six real render passes with their own six
single-face copies and a mip generation, and the strip copies' transitions
still being *whole-array* transitions. The next cut, if the phase ever
matters again at the 15 Hz rate, is the captures copying as a strip and
the transitions narrowing to the slice they touch; neither is small, and
the dial has made both optional.

---

## 7ai. The depth a multisampled target hands out (M.1)

**The symptom.** Choose MSAA in Render Settings and the whole frame goes
soft — not the edges, everything, near and far alike, as though the lens
had been left wide open on nothing. Every other anti-aliasing mode looked
right.

**The cause is one sentence in §7q that stopped being true.** MSAA gave
each colour attachment a single-sampled twin and made `GetColorTexture`
hand the twin out, so that nothing downstream had to know MSAA existed.
The depth attachment got no twin, because when that was written nothing
downstream read the depth — and both backends carry a comment saying so.

Then depth of field (9.4), motion blur (9.5), SSAO (9.6) and SSR (9.7)
each learned to reconstruct a view-space position from the scene depth,
and each of them asked `context.Depth(sceneHDR)` for it. Under MSAA that
call handed back the 4x attachment itself, and a multisampled image bound
to a `sampler2D` reads as undefined. The layers say it plainly — ten
lines of *"has VkImage created with VK_SAMPLE_COUNT_4_BIT, but OpTypeImage
has marked it as single-sampled"*, naming `u_Depth` in four different
passes — and the undefined it returned came out as "everything is at the
near plane", so the circle of confusion pinned at maximum across the
frame. The bokeh radius the demo profile asks for is 14 px, which is why
it read as a blur rather than as a bug.

**The fix is the twin the colours already had.** A target whose depth is
both multisampled *and* sampled builds a single-sampled depth image
alongside the attachment; the pass resolves into it on the way out;
`GetDepthTexture` hands it out. Nothing above the RHI changed — the four
passes, their shaders and the frame graph are untouched.

Three things it is worth being specific about:

- **Sample zero, not the average.** Vulkan asks by name
  (`VK_RESOLVE_MODE_SAMPLE_ZERO_BIT`); OpenGL's `glBlitNamedFramebuffer`
  of `GL_DEPTH_BUFFER_BIT` picks one sample and does the same thing. It
  matters that it is not an average: a depth is a *position*, and the
  mean of two positions either side of a silhouette is a place where
  nothing is. Averaging would draw a one-texel rim of invented geometry
  around every edge in the frame, and the occlusion and the reflections
  would both believe it. Sample zero is somewhere that was really there.
  It is also the only depth resolve mode Vulkan guarantees.
- **Only when something samples it.** A shadow map is never multisampled
  and nothing samples the probe faces' depth, so the twin is conditional
  on `DepthSampled` — otherwise every shadow cascade would carry a second
  full-size image nobody reads. And the flag moves: with a twin, the
  multisampled attachment is created *without* `Sampled` usage, so binding
  it to a sampler fails loudly instead of returning undefined.
- **The barrier is not the one the layout implies.** Vulkan puts a render
  pass resolve write in the colour attachment output stage whatever the
  attachment's aspect is. The colour twins get that for free — it is
  exactly what `COLOR_ATTACHMENT_OPTIMAL` implies — but a depth twin's
  layout implies the two fragment-test stages, and a barrier naming only
  those leaves the resolve unsynchronised against the transition before
  it. Sync validation reports it as a write-after-write on the first
  frame; `TransitionTo` takes an extra stage and access for exactly this
  case.

**What it costs.** One D32 image at frame size, only under MSAA: 14 MB at
1440p, 8 MB at 1080p, per frame chain. No extra pass and no shader — the
hardware resolves it with the colours. 1440p demo, Vulkan, 600 frames:
3.92 ms at MSAA 4x against 3.86 for TAA and 3.67 with anti-aliasing off.

**What still averages, deliberately.** The surface attachment SSR reads
(octahedral normal, roughness, metallic) resolves by averaging like every
other colour attachment, so a silhouette texel carries a normal blended
between two surfaces. SSAO's agreement rule (§7ae) rejects exactly that
and falls back to reconstruction; SSR traces a slightly wrong ray on a
one-pixel rim. Both are visible only if looked for, and the alternative —
a per-attachment resolve mode — is 7q's ceremony again for an artefact
nobody has been able to see.

**What let it survive a phase.** `check_depth_of_field.py` measures the
exact thing that broke, in the exact scene for it, and passed the whole
time: every render in this repository's checks was `--aa=none`. The suite
had five claims about the lens and no claim that any of them survived a
setting the user can change from a dropdown. It now renders claims 2–5
again at `--aa=msaa` and `--aa=ssaa`; reverted, that reads 23% of the
in-focus detail kept on Vulkan and 28% on OpenGL against 115% with the
fix, which is not a threshold anyone has to tune. Underneath it,
`scenetest` states the invariant where it belongs and without a scene: a
4x target you can sample the depth of hands back a single-sampled image,
at the target's size, usable as a texture.

---

## 7aj. The engine's own <cmath> (X.1)

5.0c replaced glm with the engine's own vectors and matrices, for the reason
Types.h gives: RageV should read as one engine rather than as an assembly of
libraries, and an alias hides nothing because every compiler error still names
the library. The scalars were left behind. A call site said `Math::Clamp` on
one line and `std::sin` on the next, and a game script had to know which of
three namespaces a function lived in — which is being asked to learn the
engine's history rather than its API.

`RageV::Math` now carries the whole of `<cmath>` this engine uses, and
`RageV.Mathf` mirrors it in C#. 402 call sites moved across the engine, the
editor, the tools and the sample scripts. **One `std::` survives, in
scenetest's glm oracle**, and it is commented: a comparison built out of the
thing it compares passes forever.

### The wrappers are not all forwards

Four differ from the standard library, and each difference is one this engine
already believed in somewhere else:

- **`Acos` and `Asin` clamp first.** A dot product of two unit vectors is
  analytically in [-1, 1] and numerically is not, and `std::acos` of one ulp
  past the end is a NaN. That is the same failure `Normalize` has guarded
  since 5.0c — a NaN angle becomes a NaN rotation and then an object nobody
  can find — met from the other side. Math.cpp had been writing
  `std::acos(Clamp(...))` by hand at three call sites; now it cannot be
  forgotten at a fourth.
- **`Mod` takes the sign of the divisor, `FMod` the dividend.** Both are
  provided, so a call site has to say which it means. They differ only for
  negative inputs, which is exactly when picking the wrong one is invisible.
- **`Fract` is always positive**, for the same reason: a texture coordinate of
  -0.25 is three quarters into the tile.
- **`SafeSqrt`** exists beside `Sqrt` rather than replacing it.

### Two things the templates changed underneath existing code

`Min`, `Max` and `Clamp` were declared **only for float**. So
`Math::Max(width / 2u, 1u)` converted both arguments to float, compared them
there and converted back — exact only below 2^24, and every one of those call
sites is a pixel count, a buffer size or a mip count. They are templates now,
which also *rejects* `Max(anInt, aUint)` instead of quietly picking one. The
scenetest case is `Max(16777217u, 16777216u)`, two numbers that are the same
float and different unsigned.

`Round(float)` moved from Math.cpp into the header with the other forwards.
`Functions.h` says out-of-line means "long enough to belong in a .cpp", and a
one-line forward to `std::round` is not that; it sat there only because it was
the odd one out before the rest of `<cmath>` had names.

### The C# side is a claim, not a convention

Naming both sides `Sin` is a convention. Being the *same function* is a claim,
and on three of them it is not free:

- **.NET rounds a half to even and C rounds it away from zero.** `Round(0.5)`
  is 0 in one language and 1 in the other, out of the box. A script placing a
  tile by rounding would land in a different square depending on which
  language placed it, on one value in two.
- `MathF.Acos` does not clamp, and `%` is `fmod` rather than the wrapping
  remainder.

So `Interop.EvaluateMath` takes an opcode and two floats, and scenetest walks
39 functions × 10 awkward inputs against the native ones. Reverting the
rounding mode makes it say `Round(0.500000): C# 0.000000 vs C++ 1.000000` —
which is the check being able to fail, demonstrated rather than assumed.

**Named `Mathf` rather than `Math`**, because a `RageV.Math` would be
ambiguous with `System.Math` in every script carrying both usings — CS0104,
reported at the call site rather than at the cause. The shipped scripts and
the New Script template both carry both usings.

### And one thing this found in a neighbouring check

`check_oit.py` compares a weighted-transparency render against a sorted one
and required their silhouettes to match **to the pixel**. They do not: the
faintest ring of the plume lands either side of the coverage threshold
depending on the frame — 36, 0, 76, 0 and 0 differing pixels at frames 10, 20,
30, 45 and 60, out of ~152,000 covered. The check had been passing on the
frames that happened to be zeroes, and it renders without `--frame-time`, so
which frame it got was a stopwatch. Time is pinned now and the claim is a
fraction of the covered area: ten times the worst fringe observed, and still
forty-five times smaller than the thing it hunts, since rendering the
silhouette upside down differs by 34,304 pixels. **Not a regression from this
work** — proved by stashing it and watching the same 76 pixels.

---

## 7ak. Who owns a key press (E.1)

**Reported twice: Ctrl+S does not save.** It saves perfectly when the pointer
is over the viewport, and does nothing at all the rest of the time — which is
every time it matters, because you press it *after changing something*, and
changing something means you were in the Inspector or the Hierarchy.

**The layer stack is the mechanism.** `ImGuiLayer` is pushed as an *overlay*,
so it sits above `EditorLayer`, and `Application::OnEvent` walks the stack
from the top and stops at the first layer that marks an event handled. The UI
layer's rule was:

```cpp
e.m_Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
```

`WantCaptureKeyboard` is true for keyboard **navigation**, not only for
typing, and `ImGuiConfigFlags_NavEnableKeyboard` is on. So any panel having
focus made it true, the overlay consumed the key, and
`EditorLayer::OnKeyPressed` never ran. No save, no error, no log line —
nothing to see, and nothing to search for. Every editor shortcut went the same
way: Ctrl+Z, Ctrl+N, Ctrl+O, Ctrl+P, Delete.

**The rule now asks about text input**, which is a fact about what ImGui is
doing rather than a guess about who owns a shortcut: a caret in a field is a
claim on the keyboard, navigation focus is not.

**The second path to the same symptom** is the one the owner's screenshot
showed without either of us noticing: the blocker is set inside the Viewport
panel's draw, and `ImGui::Begin` returns false when that panel is collapsed
or **behind another tab**. The Game panel shares its dock node. So looking at
the Game tab took the early return, which set nothing — leaving last frame's
values, and a viewport nobody can see reporting itself hovered. Both flags are
cleared on that path now.

**The check is a pure function**, which is the only way this was ever going to
be testable: `UiConsumesEvent(capture, isMouse, isKeyboard)` takes what ImGui
says it wants and answers who gets the event, with no context, no window and
nobody pressing a key. `CheckShortcutOwnership` states the truth table, and
the case that broke is one line of it — a focused panel does not swallow a
shortcut. Restoring the old rule fails exactly that check and nothing else.

**What this says about the bar.** Everything in this engine that a person
reaches by *pressing* something has this shape: no check renders the editor
and drives it, so the whole of the input layer is verified by using it. The
fix here was to move the decision out of the frame loop and into a function,
which is the general answer — a rule that can be stated away from the UI can
be checked away from the UI.

---

## 7al. Bindless (8.2): the split is forced in two places, and neither is the RHI

§5 called this the first place two backends would cost something real, and
the roadmap called it a decision about what the engine is. Both were written
before anyone had read the binding code with the question in hand. This
section is the result of reading it, and the answer is smaller than the
warning: **the split is forced in exactly two places -- the descriptor heap
itself, and the GLSL that reads a texture by index -- and everything else
stays common.** The RHI keeps one interface. OpenGL keeps rendering what it
renders today, through the code it renders it with today. The fork lives in
`Material::Bind` and in one block of `pbr_fragment.glsl`, and it is
falsified by a pixel comparison on the one backend where both paths run.

### Why the split is forced, and why it is not larger

**The heap.** Vulkan has an object for "a large array of textures the shader
indexes at runtime": a descriptor set whose one binding is a runtime-sized
array with `PARTIALLY_BOUND` and `UPDATE_AFTER_BIND`. OpenGL 4.5 has no such
object. `GL_ARB_bindless_texture` exists, but it has no SPIR-V route:
SPIRV-Cross *throws* on a runtime-sized descriptor array when the target is
desktop GLSL, and turns `nonuniformEXT` into `GL_NV_gpu_shader5`, which is
one vendor. Going that way would mean compiling GL shaders from raw GLSL,
which bypasses reflection and the `FlatBindingMap` the whole GL backend rests
on -- a second shader path, not a second binding path. The other GL surrogate,
a sized `sampler2D[N]`, is bounded by 32 texture units per stage, and the PBR
fragment shader already uses 25 of them. So OpenGL does not get a heap, and
that is the end of that question rather than a compromise.

**The GLSL.** `texture(u_Textures[nonuniformEXT(i)], uv)` has no GL 4.5
spelling. One source file has to produce two shaders.

**Why nothing else splits.** The renderer was already halfway there without
saying so. `InstanceData` carries the material's *scalars* per instance --
base colour, emissive, metallic, roughness, occlusion, normal scale -- so
that two objects with different values still share one instanced draw (7m).
The only thing the material set still does is bind nine textures, and the
textures are the only reason `MaterialKey` is in the sort key at all.
Bindless finishes that job; it does not start a new one. Post-processing,
2D, UI, particles, sky, IBL, shadows, the render graph and compute never
touch the material set and are not touched by this.

### The seam: a shader define and a material fork, and the RHI stays one interface

The rule that keeps the fork to two places: **split at the shader
preprocessor and at the material, never at the RHI.**

`ShaderDesc` gains a list of defines, injected as a glslang preamble and
folded into the SPIR-V cache hash. The renderer compiles the PBR shaders
with `RV_BINDLESS` when the device says it can, and without it otherwise.
The GL variant therefore never contains a runtime array, so cross-compile,
reflection and the flat binding map are untouched -- the GL backend does not
know the feature exists.

The RHI gains one capability that already existed as a field and was never
set -- `DeviceCaps::SupportsDescriptorIndexing`, with `MaxBindlessTextures`
beside it -- and one factory: `CreateBindlessTextureSet(capacity)`, which
returns an `RHIResourceSet` on which only `SetTexture(0, texture, sampler,
index)` and `Commit()` mean anything, or null when the device cannot. That
is the whole public change. On OpenGL it returns null and the caps stay
false; that is the whole OpenGL implementation. `ResourceBinding::Count == 0`
acquires a meaning it did not have -- runtime-sized -- which is what
SPIRV-Cross already reports for `sampler2D u_Textures[]`.

### The heap, on Vulkan

One descriptor set, allocated from its own pool created with
`UPDATE_AFTER_BIND_POOL`, whose one binding is `COMBINED_IMAGE_SAMPLER` x
capacity with `PARTIALLY_BOUND | UPDATE_AFTER_BIND`. **Not one per frame in
flight**, unlike every other resource set: update-after-bind is precisely
the permission to write into a set that is bound to a pending command
buffer, and the heap depends on it -- a texture registered mid-frame is
readable the same frame.

The permission has one condition, and it is the whole of the heap's
lifetime design: **a slot that a pending command buffer reads must not be
rewritten until that command buffer has completed.** So a slot is released
in three steps -- retired into a per-frame list, rewritten to the error
texture when that frame's slot comes round again (which the fence has
already guaranteed is after the GPU finished with it), and only then
returned to the free list. This is the same shape as `DeletionQueue::PerFrame`
and for the same reason.

The pipeline layout has to agree with the heap. Set layouts are built from
reflection and only from reflection (`CreateLayouts`), and pipeline-layout
compatibility requires *identically defined* set layouts -- same count, same
flags. So the device owns one canonical bindless layout, created lazily with
`descriptorCount = MaxBindlessTextures`, and hands the same object to the
heap when it is created and to any pipeline whose reflection declares a
runtime array. A pipeline that borrows it does not destroy it. The
convention that keeps this a lookup rather than a search: **the heap is set
2, binding 0, in every shader that uses it**, and set 1 -- the material set
-- is simply empty in the bindless variant. `CreateLayouts` already fills a
gap with an empty layout.

**Slot 0 is the error texture, and every slot starts as slot 0.** All
`capacity` slots are written with a 1x1 magenta at creation, so there is no
such thing as an unwritten slot: an index that is stale, uninitialised or
simply wrong reads magenta, deterministically, on any hardware, instead of
being undefined behaviour that happens to look fine on this one. This is
worth stating because of the trap below.

The renderer-side `TextureHeap` is the free list, the dedup by
`(texture, sampler)` pointer pair, and the retire chain, in common code that
only ever calls `SetTexture` and `Commit` on the RHI set. It holds textures
*weakly*: strong references live in the materials, and a texture nobody
references is one nobody's record can name, so its slot is safe to retire
once the frames that might have named it are done. A sweep at the start of
each scene retires expired entries; a lookup that finds an expired entry
under a reused pointer treats it as a miss and retires it, so a
destroy-and-reallocate at the same address between the sweep and the lookup
cannot hand out a dead slot. Samplers are held strongly; there are three of
them.

Capacity is `min(4096, maxDescriptorSetUpdateAfterBindSampledImages)`, and
a device whose limit is under 256 reports the feature absent rather than a
heap too small to be worth having. Every 1.2 desktop driver this engine has
met is in the millions.

### The material record, and where the index rides

In the bindless variant the material set is gone. What it used to say is
said in two places instead:

- The scalars stay in `InstanceData`, exactly as they were.
- The rest -- eight heap indices, `MapFlags`, `Specular`, `HeightScale`,
  `UvTransform` -- is a 64-byte `GpuMaterial` record in a per-frame storage
  buffer at set 0, binding 13, beside the lights. `InstanceData.Indices.z`
  is the record index, carried to the fragment stage as a flat varying, the
  way the probe index already is in `.y`.

Records are gathered per frame, not kept: `EndScene` walks the sorted
pending draws, assigns each distinct material an index for this frame,
writes its record, and stamps the index into the instance. A frame's records
cost `materials x 64 bytes` and are rebuilt every frame, which is nothing,
and it means no second free list, no dirty tracking across frames in flight,
and no state on the material at all beyond the ability to write its own
record from what it already holds. Building a stable global material table
was considered and rejected for exactly those three costs. A record could
instead have gone into `InstanceData` directly and skipped the indirection;
that grows every instance by a quarter to carry data identical across the
thousand instances of one material, and the shadow pass fetches
`InstanceData` too. The lights already answered this question the same way.

The batch key follows: on the bindless path `Material::GetBatchKey` mixes
nothing but the sampler, so runs merge across materials sharing a mesh, and
a run breaks on mesh alone. On the bound path it is unchanged. That is the
one-line branch in `Renderer3D`.

### One shader source

The fork in `pbr_fragment.glsl` is a declaration block and nothing below it:

```glsl
#ifdef RV_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 2, binding = 0) uniform sampler2D u_Textures[];
layout(std430, set = 0, binding = 13) readonly buffer MaterialBlock { GpuMaterial Materials[]; } u_Materials;
GpuMaterial g_Material;                 // fetched once at the top of main
#define u_Material     g_Material
#define u_BaseColorMap u_Textures[nonuniformEXT(g_Material.Maps0.x)]
// ... one line per map
#else
layout(set = 1, binding = 0) uniform MaterialData { ... } u_Material;
layout(set = 1, binding = 1) uniform sampler2D u_BaseColorMap;
// ... as today
#endif
```

Every `texture(u_BaseColorMap, uv)` and every `u_Material.MapFlags` below
the block compiles unchanged in both variants. The lighting is one piece of
code with two front doors, which is the whole point of putting the fork at
the preprocessor: the six hundred lines that could drift do not exist twice.
`pbr_skinned` includes the same file and gets both variants for free.

`nonuniformEXT` is on the *descriptor* index only. Indexing the record
buffer by a per-instance value is ordinary memory addressing and needs no
qualifier; indexing a descriptor array by a value that differs across the
instances of one draw does, and once runs merge across materials it always
differs. Leaving it off is undefined behaviour that works on most hardware,
which is the worst kind.

### The checks, and the one trap worth stating now

**On Vulkan both paths run**, which is what makes this checkable at all:
`--bindless=off` compiles the bound variant on a device that supports the
heap. The demo scene rendered both ways must be pixel-identical, and the
check is falsified by breaking one index -- swap two record fields and the
compare must fail. That one comparison covers the fork end to end.
OpenGL's existing checks are the regression suite for the bound path, and a
`CheckBindless` in scenetest covers the heap on its own: register, sample by
index, release, and the slot not reused until the frames that could have
read it are done, skipped with a stated reason where the caps say no.

**The trap: an out-of-range index into a partially-bound array produces no
validation message.** The layer cannot know which element a shader will
read, so ordinary validation says nothing, and GPU-assisted validation is
the only thing that catches it -- which is why the error texture in slot 0
and every unwritten slot exists, and why the check suite runs the demo once
under GPU-AV. A wrong index in this engine is a magenta object, not a
crash and not silence.

### What it maps to, if another backend arrives

DX12's binding model *is* a heap -- one shader-visible descriptor heap, with
`NonUniformResourceIndex` for the qualifier and, from SM 6.6,
`ResourceDescriptorHeap[i]` with no declaration at all. SPIRV-Cross's HLSL
backend emits runtime arrays and `NonUniformResourceIndex` for SM 5.1, so
the `RV_BINDLESS` variant cross-compiles without a third source, and a DX12
backend would implement even the *bound* `RHIResourceSet` as a table in the
same heap. DX11 is fixed slots -- 128 SRVs, and dynamic indexing of a
resource array is SM 5.1, which is DX12 -- so it would land on the bound
side beside OpenGL and take the existing path. The seam absorbs either
without a third fork.

### What is deliberately not moved

2D and UI already do the poor version -- a `sampler2D[32]` and a per-quad
index, flushing at 32 -- and would move in forty lines each. They do not
move now: the benefit appears only past 32 distinct textures in one batch,
which nothing in the tree approaches, and each subsystem moved is another
fork with a bound twin to maintain. Post-processing never moves: every pass
reads one to four *specific* textures known when the pass is written, which
is the opposite of the situation bindless is for, and its targets are
transient and resize, which would churn the heap for nothing.

**What this buys today, plainly: not frame time.** A thousand meshes draw in
1.9 ms and are nowhere near a binding wall. Runs merge across materials, and
that is the visible effect. The reason to do it is that 8.12 and 8.3 are
both behind it, and this is the smallest version that unblocks them without
the OpenGL backend learning anything.

### What building it found

Everything above was written before the code. Four things came out of
writing the code that the design did not predict, and they are the record
that matters.

**The parity check failed on its first run, and the bound path was the one
that was wrong.** 1,036,879 pixels differed, all on the courtyard's walls.
The wall and the plinth share every brick map and differ in `Tiling` (9 x
2.4 against 2.2 x 1.6) and `HeightScale`; `Material::GetBatchKey` mixed the
maps, the sampler and `MapFlags` -- and not `UvTransform`, `HeightScale` or
`Specular`, which live in the material's uniform block and nowhere per
instance. So on the bound path the two materials were one run, the nearer
plinth came first, its block was bound for both, and **every wall in the
demo has been drawing at the plinth's tiling since the day `UvTransform`
joined the block.** Nobody noticed, because it still looked like bricks. The
bindless path writes one record per material and was right from the start;
the key now mixes the block's scalars, and after that the two paths agree
to zero pixels across five runs. This is the whole argument for a second
implementation stated as a number: a check that compares an implementation
against itself passes forever, and this one compared it against another.

**GPU-assisted validation does what the trap paragraph said, and ordinary
validation does exactly nothing.** With one record slot deliberately pushed
past capacity, `--validation=on` exits 0 with zero `[Vulkan]` lines and the
object draws magenta; `--validation=gpu` reports *"Index of 5004 used to
index descriptor array of length 4096"* on every draw, by set and binding.
The flag exists now (`EngineConfig::ValidationGpuAssisted`, layer setting
`gpuav_enable`); it turns sync and core validation off for the run, on the
layer's own advice, and prints three "adjusting settings" advisories at
startup that are its own. `check_bindless.py` runs it and permits exactly
those.

**The parity check covers the maps the demo samples, and only those.**
Falsifying it by making the normal-map slot read the base colour: 1,432,357
of 1,440,000 pixels differ. Falsifying it by making the *emissive* slot read
the base colour: **zero pixels differ**, because no courtyard material has an
emissive map and the shader reads a map only when its flag is set. So the
check proves base colour, normal, occlusion, roughness and height, and
proves nothing about emissive, metallic or specular. `CheckBindlessHeap`
samples every slot it registers through a compute shader, so the *heap* is
covered end to end; the *record fields* for the three unused maps are
covered by inspection. Stated in the script, so nobody reads its green as
wider than it is.

**The RHI change was as small as promised, and one line smaller in the
Vulkan pipeline than expected.** `BindResourceSet` needed only to cast to a
common base rather than the per-frame class; `CreateLayouts` needed a
borrowed-layout flag so the destructor does not destroy the device's; and
the resource-set base already carried `arrayIndex`, so the heap's write path
is the existing one with a different lifetime. The whole diff is ~600 lines,
of which ~200 are Vulkan and ~150 are the two forks; the OpenGL backend
gained six lines, all of them the word "no". Numbers at landing: 4096 slots
on this device (the driver's limit is in the millions), 1606 scenetest
checks on Vulkan and 1585 on OpenGL, zero validation lines, `check_ssr`,
`check_oit`, `check_depth_of_field`, `check_ssao` and `check_smaa` unchanged.

---

## 7am. Ray tracing (8.12): a ray before a hit shader, and a shadow before a reflection

The roadmap wrote 8.12 down as "acceleration structures and ray queries,
wired into the SSR trace as the fallback when the screen-space walk misses".
That is still the destination. This section is about what has to exist
before it, and about the first thing that should be built on it -- which is
not a reflection.

### Two tiers, and the whole plan is the distinction

**Ray queries** are a function call from a shader that already exists: a
fragment or compute stage asks "does this ray hit anything, and where", and
gets an answer. No new pipeline type, no shader binding table, no raygen or
hit stages. **Ray tracing pipelines** are all of those, and are the thing
people picture. Ray queries come first, and everything in this section is
built on them alone; pipelines are not needed for anything on the roadmap
and are not planned.

What a ray query cannot do is *shade the thing it hit*. It returns a
primitive index, an instance index and barycentrics; turning that into a
colour means reading that mesh's vertices and that instance's material with
nobody having bound them -- every vertex and index buffer reachable by
address, every material record and every texture reachable by index. The
textures are 8.2. The buffers are not yet anything. **Hit shading is the
large half of ray-traced reflections, and it is a bindless-buffers project
before it is a ray-tracing one.**

### So the first ray is a shadow ray

A shadow needs no hit shading. From the point being lit, trace toward the
light; if the ray hits anything at all, the point is in shadow. One ray per
fragment per light, opaque geometry, stop at the first hit -- the cheapest
query there is, and the one that puts a real ray on screen with nothing but
the acceleration structures underneath it.

It is also the ray that removes a limitation the notes already record. 3.5
built cascaded shadow maps with two biases because one was not enough --
back-face rendering against acne, a normal offset against the acne that
remains -- and `ShadowNormalOffset`'s own comment says there is no value
that has neither acne nor detachment. A traced ray has neither: the shadow
starts where the caster touches the ground, exactly, and a surface edge-on
to the light is not a texel spanning a metre of depth. It also has no
`ShadowDistance`: the cascades stop at forty metres because past that the
texels are worse than nothing, and a ray does not have texels. So the
measurement is already stated: agree with the maps where the maps are right,
and be right where they are not.

Stage 1 is therefore: **acceleration structures in the RHI, ray queries
proven in scenetest, and ray-traced directional shadows as a shadow mode
beside the cascades.** Reflections -- the SSR fallback -- come after, on the
same structures, once buffers can be reached by address.

### The RHI shape

Two resource kinds, one build, one binding, one usage bit, one cap:

- **`RHIAccelerationStructure`**, bottom or top level. A bottom-level one
  (BLAS) is one triangle mesh -- a vertex buffer with positions in its first
  three floats, an index buffer -- and is built once, immediately, the way a
  texture is uploaded. A top-level one (TLAS) is a list of *instances*, each
  a BLAS with a transform, and is rebuilt every frame from whatever the
  scene has in it. `CreateBottomLevelAS(geometry)` and
  `CreateTopLevelAS(maxInstances)` on the device.
- **`RHICommandList::BuildTopLevelAS(tlas, instances, count)`**: writes the
  instance list, records the build, and ends with the barrier that makes the
  structure readable by any shader stage. The barrier is inside the call
  rather than a `BufferSync` kind because there is exactly one thing a
  built TLAS is for, and a caller that could forget the barrier would.
  **Must be recorded outside a render pass** -- building is not permitted
  inside one, and the frame has a place for it: `Scene::RenderShadows`,
  which already runs before the graph and already walks every mesh.
- **`RHIResourceSet::SetAccelerationStructure(binding, tlas)`**, and
  `ResourceType::AccelerationStructure` recovered from reflection, so a
  shader declaring `uniform accelerationStructureEXT` gets a layout that
  matches without anyone writing it down twice.
- **`BufferUsage::AccelerationStructureInput`** on a mesh's vertex and index
  buffers. The build reads them by device address, and a buffer that was not
  created with that usage cannot be read that way -- so `Mesh` sets it when
  the device can trace, and a mesh created before that decision cannot be
  traced. Stated because it is the kind of requirement that fails at build
  time with a message about an address.
- **`DeviceCaps::SupportsRayQuery`**: the three extensions
  (`VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
  `VK_KHR_deferred_host_operations`) and the three features
  (`accelerationStructure`, `rayQuery`, `bufferDeviceAddress`), all present.
  OpenGL: false, `CreateBottomLevelAS` null, and the shadow-map path is what
  it always was. This is the first feature with no OpenGL implementation at
  all -- not an awkward analogue, none -- and the roadmap said it would be.

On Vulkan the buffers behind all of this are ordinary VMA buffers with the
device-address usage; the allocator itself is created with the
buffer-device-address flag; scratch memory for a build is sized by
`vkGetAccelerationStructureBuildSizesKHR` and, for the TLAS, kept beside it
so a per-frame rebuild allocates nothing. **One TLAS per frame in flight**,
like every other per-frame resource, because the previous frame's fragment
shaders may still be tracing into theirs. Instances are
`VkAccelerationStructureInstanceKHR` -- a 3x4 transform, a 24-bit custom
index, an 8-bit mask, flags, and the BLAS's device address -- in a
host-visible buffer the command list fills; the address is why the RHI
takes `AccelerationInstance{ Transform, Blas, ... }` and packs it in the
backend, rather than asking the renderer for a struct only Vulkan can
complete.

### The renderer

`Mesh` builds its BLAS on first use, when the device can, and caches it --
static geometry, built once, the same lifetime as the vertex buffer. A
skinned mesh builds one too, **from its bind pose**: its posed vertices
exist only inside the vertex shader, and refitting a BLAS from them needs a
compute skinning pass writing to a buffer, which is stage 2. So in stage 1
the fox casts the shadow of its bind pose, moved by its transform -- present
and slightly wrong, which is better than absent, and stated.

`Scene::RenderShadows` is where the frame decides what shadows are. Under
`ShadowMode::Maps` it renders cascades as it always has. Under
`ShadowMode::RayTraced` on a device that can, it skips the cascades and
instead walks the same mesh view, appends an instance per mesh -- BLAS plus
world transform -- and records one `BuildTopLevelAS` into the frame's
command buffer. Local lights keep their spot and point maps either way in
stage 1: replacing them is one more ray per light per fragment inside the
cluster loop, and it is stage 2 with the skinned refit. `Renderer3D` binds
the frame's TLAS at set 0, binding 14, in the scene set, and compiles the
PBR shaders with `RV_RAY_SHADOWS` when the mode is on -- the same seam 8.2
opened, a define, so `ShadowFactor` becomes:

```glsl
#ifdef RV_RAY_SHADOWS
rayQueryEXT q;
rayQueryInitializeEXT(q, u_SceneAS,
    gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF,
    worldPos + N * offset, 0.0, L, 1e4);
rayQueryProceedEXT(q);
return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
#else
... the cascades, unchanged ...
#endif
```

and everything that calls `ShadowFactor` does not know which it got. The
offset is along the *geometric* normal, a few millimetres scaled by the
slope -- the same shape as the map's normal offset and for the same reason,
self-intersection, but a hundredth of the size, because a ray has no texel
to clear. Opaque and terminate-on-first-hit: a shadow ray does not care
what it hit or where, only whether. The result is hard. A directional light
has an angular size and a real penumbra needs several rays and a filter
across frames; that is a later stage and it is not this one, and the check
below measures the hard edge as hard rather than pretending.

The mode is a `RenderSettings` field -- `ShadowMode Shadows`, `Maps` or
`RayTraced`, default `Maps` -- because it is a judgement about the hardware
(7s), like everything else in that block, and defaulting to `Maps` means no
existing project changes appearance. Where the device cannot, `RayTraced`
falls back to `Maps` and the log says so, once. `--shadows=maps|rt` on the
command line, so the check renders the same scene both ways without editing
the project.

### The checks

**`CheckRayQuery` in scenetest**, before any of the renderer exists: one
triangle in a BLAS, one instance in a TLAS, a compute shader that traces a
handful of rays and writes back hit distances. Rays aimed at the triangle
report its distance; rays aimed beside it report a miss; a ray from behind,
with back-face culling, misses; the transform moves the hit. Skipped with a
stated reason on OpenGL. This is the check that says the acceleration
structures are real, independent of any picture.

**`check_ray_shadows.py`**, on a fixture the light is low over and shining
toward the camera: a near box and a far wall at 150 m. Claims, as measured
at landing (all pass, Debug, both backends where they apply):

1. Traced and mapped shadows are the same shape near (IoU 0.944) and far
   (IoU 0.946). Where the maps are right, the rays agree with them -- and
   this judges the ray's origin offset from *above*: too large detaches.
2. The traced edge is hard: 0 px between 10% and 90% of the swing, against
   the maps' 6.
3. Open sunlit floor has no self-shadowing: 0.000000 of pixels dark. This
   judges the offset from *below*: too small is a moire.
4. Both frames reproduce; OpenGL asked for `rt` logs the fallback and its
   maps agree with Vulkan's (IoU 1.000).

Falsified by negating the ray direction: IoU collapses to 0.000 near and
far.

**And one claim the design planned that the fixture refuted.** "Past
`ShadowDistance` the maps draw no shadow" is not true of these maps: the
setting bounds the cascade *fit*, but the last cascade's orthographic
footprint on a receding ground plane reaches far past it and the sampler
clamps to the edge texel outside the map -- so the wall's foot at 150 m is
shadowed by the maps too. The claim is not made. What the rays do have that
the maps do not, at any distance, is the hard edge and the absence of a
bias; the check states those instead.

**The traps worth stating now.** A ray query inside a fragment shader
inside a render pass is fine; the *build* is not, and the frame's one
correct place for it is before the graph. Buffer device address is a
creation-time property, so a mesh loaded before the device said it could
trace has buffers that cannot be traced. And the shadow ray's origin offset
is the whole of its correctness: too small and every surface shadows
itself in a moiré, too large and the shadow detaches -- the same two
failures as the maps, three orders of magnitude smaller, and the check's
first claim is what says the value chosen is right.

### What comes after, on the same structures

Stage 2: local-light shadow rays inside the cluster loop, and a compute
skinning pass so a skinned BLAS is refit each frame from posed vertices.
Stage 3: hit shading -- vertex and index buffers reachable by address, a
mesh table indexed by the instance's custom index, and a shade of the hit
point through the material record and the heap -- and with it the SSR
fallback the roadmap named, judged against 9.9's exactness fixture. Each
is a section of its own when it happens. *Stage 2 is §7an.*

---

## 7an. Ray tracing, stage 2 (8.12): every light gets the ray, and the fox's shadow runs

Stage 1 (§7am) left two things stated as limits: spot and point lights kept
their maps under `RayTraced`, and a skinned caster traced as its bind pose.
Neither is a new mechanism -- the structures, the query, the define and the
shadow mode all exist -- so this section is short on design and long on the
two places the existing design turned out to have a wrong assumption in it.

### Local lights: the ray is the same ray, with a length

The stage 1 shadow ray has no `tMax` worth speaking of: a directional light
is infinitely far, so the ray goes until it hits or `1e4`. A spot or point
light is a *position*, and the only change a shadow ray needs is to stop
there: origin at the surface (same geometric-normal offset, same reason),
direction toward the light, `tMax` the distance to it. A caster beyond the
light does not shadow, which the length says and a map's far plane said
before. So the cluster loop's fork is: kind 1 traces along `L` to infinity;
kinds 2 and 3 trace along `L` to the light. `SpotShadow` and `PointShadow`
-- the map lookups -- are simply not compiled under `RV_RAY_SHADOWS`; the
loop reaches one `TraceShadow` for every casting light instead of three
functions for three kinds.

**And the slot caps go away under the rays.** `ShadowMap::kMaxLocal` (four
spot maps, four point cubes) exists because each map is a scene render, and
`Scene::RenderShadows` stops assigning past it with a warning. A ray is not
a scene render: the cost of the fifth casting spot light is one more query
per fragment inside its cluster, and there is no map to run out of. So under
`RayTraced` every light with `CastShadows` is assigned its kind and *no
slot*, and none of them renders a map. The TLAS is built when any light
casts, not only when a directional one does -- stage 1 returned early
without a sun, which was right for cascades and wrong for a scene lit by a
lantern.

**And the setting is a checkbox.** 7am made it a two-way `ShadowMethod`
enum, `Maps | RayTraced`; the owner asked for ray tracing to be one switch
in Render Settings, and that is the better shape now that the rays are not
a shadow *method* but a way of answering every light -- and, in stage 3,
reflections -- on the same structures and the same hardware question. So
`RenderSettings::RayTracing` is a `bool`, off by default, shown as a
checkbox with the cascade dials hidden under it; `ResolveRayTracing`
replaces `ResolveShadowMode`, `--raytracing=on|off` replaces
`--shadows=maps|rt`, and `RenderSettings.RayTracing` reaches C#. It needs
no restart: flipping it recompiles the lit shaders on the spot, both ways
(`Renderer3D::SetRayTracedShadows` already did), so there is no
"restart to apply" dialogue of the kind the backend picker has -- the
tooltip says so, and the log says once when a device without ray queries
was asked and given the maps instead.

**A stale cap found on the way.** `ShadowMap::kMaxLights = 8` -- "must
match the shader's MAX_LIGHTS" -- was the 2D shader's array size, and the
3D lights have lived in a storage buffer since 3.8a. It survived as the size
of the assignment table, which meant the ninth light in a scene could never
cast a shadow of any kind however many slots were free. The table is a
vector now, sized to the scene, and the constant is gone; the maps gain
from this too.

### Skinned casters: pose the vertices once, in compute, and refit

A skinned mesh's posed vertices exist only inside its vertex shader (3.7c);
an acceleration structure reads a buffer by address. So the pose has to be
written to a buffer, and the only stage that can write one is compute. The
pass is the vertex shader's skinning with the rest removed: read the
`SkinnedVertex` (position, four joints, four weights), read the four bones
from the same matrices the lit pass will be given, write `weights · bones ·
position` -- as a `vec4` at a stride of sixteen, into a buffer created with
`AccelerationStructureInput`. Positions only: a shadow ray needs the shape
and nothing else, and the normal, texture coordinate and skinning weights
stay where they were.

**Refit, not rebuild.** A bottom-level structure built with `ALLOW_UPDATE`
can be *updated* in place from moved vertices as long as the topology is the
same, and a skinned mesh's topology never changes -- the index buffer is the
mesh's own. Update is a fraction of a build, and it is what every engine
does for characters. So `AccelerationGeometryDesc::Dynamic` says: create
for update, keep the scratch, do not build at creation (the posed buffer is
empty then), and `RHICommandList::BuildBottomLevelAS(blas)` records a full
build the first time and an update after -- with the barrier a TLAS build
after it needs, inside the call, for the same reason the TLAS's is. The
call re-specifies nothing: the structure remembers its geometry, and the
geometry's addresses point at the posed buffer, which is what changed.
`BufferSync::AccelerationBuild` names the read side of the barrier between
the compute write and the build (build inputs are a *shader read* at the
build stage, per the specification, and not an acceleration-structure
read).

**One of everything per frame in flight, and per caster.** The TLAS is
per frame in flight because the previous frame's fragment shaders may still
be tracing into it; a BLAS the TLAS references and a posed buffer the BLAS
reads are in the same position, so a skinned caster owns `frames` posed
buffers, `frames` structures and `frames` sets, and refits this frame's.
Casters are a pool in `RayShadows`, reused by index in the order the scene
walks them -- the same shape as the TLAS capacity and the per-frame slots
everywhere else -- and a slot whose mesh changed since it was last used
recreates its buffer and structure for that frame (a full build follows,
because a refit needs the previous shape to have been this mesh). A skinned
mesh with no animator, or an animator with no pose yet, keeps the static
bind-pose structure the mesh already caches: nothing to pose, nothing to
refit, and the editor -- where clips do not run outside Play -- traces the
same structure it did in stage 1.

The bones the compute pass reads are the animator's `Skinning` matrices --
the same vector the lit pass uploads at binding 11 -- collected into a
per-frame buffer of `RayShadows`' own, because `RenderShadows` runs before
`BeginScene` and the lit pass's buffer does not exist yet. The order in the
frame is then: pose every skinned caster (one dispatch each, bones by
offset), one barrier per posed buffer, refit every dynamic structure, build
the TLAS, then the graph. All of it before the first render pass, where
stage 1 already put the build.

### The checks

`CheckRayQuery` in scenetest gains the refit: a dynamic structure over a
triangle whose vertices are then *moved* in the buffer and the structure
refit; a ray that hit the old position misses, and one aimed at the new
position reports the new distance. This is the RHI's claim, independent of
skinning.

`check_ray_shadows.py` gains a second fixture, `ray_shadows_local`: a dark
floor with no sun, a spot light and a point light each throwing a box's
shadow onto it, and the fox running under a third spot. Claims:

1. **A spot's traced shadow and its mapped shadow are the same shape**
   (IoU ≥ 0.9), and **a point light's are** -- the local-light ray agrees
   with the map where the map is right, which judges the ray's origin,
   direction and length together.
2. **The fox's traced shadow is its pose, not its bind pose**: at the same
   frame the traced shadow and the mapped one -- the maps render the posed
   fox through the skinned depth shader -- overlap to IoU ≥ 0.85 (a rougher
   bar than the boxes': the fox is small and the map's filter is a
   texel wide either way); and **the traced shadow at frame 30 differs from
   the one at frame 45** (IoU ≤ 0.95, while the boxes' hold at ≥ 0.99),
   because the fox moved and its transform did not.
   Stage 1 fails the second claim exactly: a bind-pose structure moved by a
   constant transform gives the same shadow every frame.
3. Frames reproduce; OpenGL falls back and its maps agree.

Falsified by (a) making `TraceShadow` ignore `tMax` for local lights --
a lid sits just beyond each light, and a ray that overshoots meets it, so
every local shadow region goes dark and the IoU falls -- and (b) skipping
the refit -- the fox claim 2 fails on both halves. Both restored before
landing; the numbers are below.

### What building it found: the numbers, and the owner's flicker

Measured at landing (Debug and Release, both backends where they apply):
the spot's traced shadow and its mapped shadow overlap to IoU 0.977, the
point light's to 0.981; the fox's traced shadow overlaps its posed, mapped
shadow to 0.946 at frame 30 and differs from its own frame-45 shadow at
IoU 0.854 while the boxes' shadows hold at 1.000; five casting spots warn
under maps and not under rays; OpenGL falls back and its maps agree with
Vulkan's at 1.000. Falsified as planned: tMax ignored collapses the spot,
point and fox IoUs to 0.673, 0.666 and 0.712 (the lids over the lights are
what make that measurable -- a ray that overshoots meets one); the refit
skipped gives the fox 0.670 against the maps and 1.000 between frames, the
bind pose exactly. `CheckRayQuery` gained the refit (a dynamic structure
built from the command list, its vertices moved three units and refit; the
ray that hit at t = 5 hits at t = 8 and the just-long-enough ray falls
short): 1628 checks on Vulkan under validation, zero validation lines.

**And a flicker the owner reported in the editor viewport, which turned out
to be SSAO's and not the rays'.** The demo's courtyard walls pulsed dark in
the editor and nowhere else. Consecutive frames from separate runs are
separate clocks, so `--screenshot-count=N` now writes N consecutive frames
from one run as `<stem>_<frame>.png` -- the tool this needed -- and forty
frames showed the left wall drop by a third every eighth frame and the
right wall alternate every second: a period of eight is the temporal
jitter's phase. `--aa=none` was still; the runtime under TAA was still;
`--raytracing=on` still flickered, so not the shadows; the light grid's
tile ranges, logged, did not move; the Game panel closed and the viewport
grid off changed nothing; and `AmbientOcclusion: false` in the profile
stopped it dead -- and brightened the right wall by half, so the AO had
been wrong on it *every* frame, not only the flickering ones. A diagnostic
build of the pass that wrote the written-versus-reconstructed dot as the
occlusion showed the reconstructed normal on the grazing walls at nearly
ninety degrees to the written one, whole faces at a time, on the frames
that went dark. The cause: `ReconstructedNormal` chose the cross product's
winding by the sign of `normal.z`, "toward the camera" -- and a wall seen
along its length has a normal with almost no z, so a noise-sized component
decided which way the whole normal faced, half the time *into* the wall.
Every tap then sat inside the wall, read the wall in front of it, and the
face went a third darker; the jitter moved the depth content under the
texel grid, the noise changed sign with the jitter index, and the wall
came and went. Two lines fix it: the winding is chosen against the ray
from the eye to the point (`dot(normal, centre)`), which meets any visible
surface at a healthy angle whatever its slant; and the reconstruction
snaps its uv to the depth texel's centre before turning depth into a
point, because the pass runs at half resolution and point-samples a depth
that belongs to a texel centre up to half a texel away -- on a grazing
wall, centimetres of depth, and the five points the normal is built from
were never quite on the plane. `check_ssao.py` gained the claim that would
have caught it: the brick wall under TAA across one full jitter phase,
eight frames in one run, AO factor at one in every frame and brightness
drifting under a level (0.04 measured). The old sign test back: 57 levels
of drift, p10 0.135, and the wall failing at `--aa=none` too. The runtime
never showed it because its camera does not see the walls that grazing;
the 9.8b fixture held at 1.000 by the sign falling the right way at that
one camera and jitter -- which is what "a check that measures one frame of
a temporal effect" is worth, and why the new claim spans the phase.

### What stays stated

The edge is still hard, for every light kind: a real penumbra is several
rays and a filter across frames, and that is not this section (§7ao took
reflections and occlusion, and left the penumbra where it was). Skinned
casters are posed by a second copy of the skinning arithmetic, in compute;
if the vertex shader's skinning changes shape (a fifth influence, dual
quaternions) this pass changes with it or the shadow stops matching the
mesh -- stated because the two are in different files. Hit shading and the
SSR fallback are stage 3: §7ao.

---

## 7ao. Ray tracing, stage 3 (8.12): a hit is shaded, and the two screen-space stand-ins get their traced twins

Stages 1 and 2 could ask a ray *whether* it hit. This stage asks *what*,
and with that answer two effects that were approximations of light
transport in screen space -- reflections (SSR, 7ad-7af) and ambient
occlusion (SSAO, 7ac-7ae) -- get ray-traced variants. The owner asked for
both, off by default, as options under the Ray tracing checkbox, with the
post profile's own SSR and SSAO rows greyed and a note when the traced
form is what runs.

### Hit shading: an instance table, and buffers by address

A ray query returns an instance custom index, a primitive index and
barycentrics. Turning that into a colour needs the mesh's vertices and
indices and the surface's material, none of which is bound. So:

- **A ray-instance table** at set 0 binding 15, one record per TLAS
  instance in the order the structure was built (the custom index *is* the
  row): the vertex buffer's and index buffer's device addresses, the
  vertex stride in words, a flag for a posed (skinned) caster whose
  positions live in the compute-written buffer while its normals and UVs
  stay in the mesh's, the material record index (7al's `GpuMaterial`, so
  the heap answers the textures), and the instance's own scalars -- base
  colour, emissive, metallic, roughness -- which ride the draw instance and
  are nowhere else. `RayShadows` remembers mesh, material and params
  beside each instance it adds; `Renderer3D::EndScene` writes the records
  through the same material index the draws use, so a material seen only
  in a reflection still gets a record this frame.
- **`GL_EXT_buffer_reference`** in the shader: `layout(buffer_reference)
  buffer` types over `float[]` and `uint[]`, constructed from the
  addresses. The RHI grows one method, `RHIBuffer::GetDeviceAddress()`
  (Vulkan real, OpenGL zero) -- the buffers already carry the address bit
  because the structures build from them.
- **Bindless is required**: without the heap a hit cannot sample its
  material, so `RayTracedReflections` resolves to off on the bound path
  (`--bindless=off`) and says so once. Nothing new is forked for it.

Shading a hit is a *simplified* copy of the lit pass, and stated as such:
base colour and emissive through the material and heap, the interpolated
vertex normal (flat for a posed caster -- its normals were not posed),
every light in the buffer unclustered (a hit is not on screen, so it has no
cluster) with one shadow ray toward the sun, sky irradiance for ambient,
and no normal map, no parallax, no local-light shadows, no occlusion. A
mirror of a brick wall shows the wall's colour and lighting; it does not
show the mortar's bump. That is the trade every real-time engine makes
here, and the fixture that judges it uses emissive geometry, where the
simplified shade *is* the exact one.

### Ray-traced reflections: in the lit shader, where SSR's answer already enters

7af moved the SSR blend *into* the PBR shader so the traced radiance goes
through the split-sum weight exactly as the probe's does. The ray goes in
at the same line: under `RV_RAY_REFLECTIONS`, a glossy pixel traces the
mirror direction from its surface, shades the hit as above (miss: the sky
cube at level 0), and replaces `prefiltered` by a weight that falls from
one at roughness 0.25 to zero at 0.6 -- a mirror ray answers a mirror, and
a rough surface's blurred probe is what several dozen jittered rays would
converge to at many times the cost. This is not the SSR *fallback* the
roadmap once phrased -- SSR first, trace on a miss -- because with a ray
per glossy pixel already paid for, a screen walk in front of it is only
another way to be wrong on-screen; the SSR passes simply do not run when
the traced form is on, and its previous-frame reprojection is not needed
either: the ray is this frame's. Cost: one query plus a hit shade per
glossy pixel, so a scene of rough surfaces pays almost nothing.

### RTAO: the SSAO chain with a different first pass

SSAO is four passes: occlusion at half resolution, two blurs, apply. RTAO
replaces only the first: `rtao_compute.rvshader` reads the same depth and
surface attachment, reconstructs the same position, chooses its normal by
SSAO's rule (below), and casts the taps as short rays into the TLAS --
`tMax` the profile's `AoRadius`, one per tap, the same golden-angle spiral
and per-pixel hash rotation as SSAO's so the frame is deterministic, the
disc lifted onto the hemisphere cosine-weighted. Occluded is a hit. The
blur and the apply are the SSAO ones, unchanged, so the profile's radius
and intensity dials drive both forms and only the *toggle* changes hands.
`PostProcess::Dispatch` gains an optional acceleration structure at
binding 4; the shader is compiled only where ray queries exist, because
SPIRV-Cross cannot say `accelerationStructureEXT` in GLSL and a compile
that fails on OpenGL would take the whole post stack down with it.

**The normal, and 9.8b's mistake made a second time.** The first draft
took the *written* normal wherever there was one, reasoning that a ray does
not care whether the normal agrees with the depth's slope -- that
agreement test (7ae) was about SSAO's depth taps, and a ray has no depth
taps. `check_ssao.py`'s brick-wall claim refuted it: under RTAO the open
wall's AO factor was 0.972 at the tenth percentile (bar 0.995), 0.910 at
worst. The written normal is the shading normal, after the normal map, and
the rays are cast into the *geometry* -- which is flat where the map says
it is not; a hemisphere hung on the map's tilt sends its low rays into the
wall. So the pass takes the depth buffer's slope (its own
`ReconstructedWorldNormal`, with 7an's ray-facing sign and texel snap) and
replaces it with the written normal only within 7ae's 16 degrees, where
the written one is the smoother estimate of the same thing -- and one
guard SSAO's kernel does not need: SSAO's lowest tap sits 24 degrees above
the plane, so a normal inside the tolerance keeps every tap above the
geometry, but the cosine kernel's lowest ray sits 12 degrees up and a
written normal tilted 12-16 degrees would still send it into the wall. A
ray under the depth's slope is not cast and counts as open. With both, the
wall holds at 1.000 (p10) and the seam still darkens 15.17 (from 15.25
under the wrong rule: the difference is the low rays that were hitting the
wall the seam stands against, which they should). The general lesson is
7ae's, restated for rays: occlusion by a body has to be measured against
the normal that body implies, and the normal map is not the body.

### The switches, and the note in the profile

`RenderSettings::RayTracedReflections` and
`RenderSettings::RayTracedAmbientOcclusion`, both `false`, shown in Render
Settings only while `RayTracing` is ticked (`OnlyWhen`), overridden per
run by `--rt-reflections=on|off` and `--rt-ao=on|off`, mirrored in C#.
They resolve through the same chain as the checkbox itself: on a device
without ray queries they are off; reflections additionally need bindless.

**And the whole block is offered only where it can run** -- the owner's
condition, stated at landing: none of the ray-tracing rows appear when the
running API is OpenGL. `OffersRayTracing` gates the checkbox itself on
`RayShadows::IsAvailable()` and on `ShadowsEnabled`; the two options gate
on that and the checkbox (reflections on `Renderer3D::IsBindless()` too);
the cascade dials return whenever the rows go, since a ticked box on a
device that cannot trace renders maps. Absent, not greyed: greyed means
"applies, taken over", and on OpenGL there is no "on" for these to be. The
values are kept in the project either way, so a project authored beside an
RTX and opened on OpenGL keeps its choices for the next time it is opened
where they apply. Shadows is the parent because the rays ride on the
shadow pass -- the structure is built in `Scene::RenderShadows` and the
lit shader declares it under `RV_RAY_SHADOWS` -- so `ResolveRayTracing`
says no when shadows are off, which takes reflections and occlusion with
it (they resolve through it), and the lit pass is told before the
shadows-off return rather than after: switching shadows off while tracing
used to leave the pass believing it still traced, into the empty structure.
When one is on, the post profile's row for its screen-space twin --
`AmbientOcclusion`, or `ScreenSpaceReflections` with `SsrMaxDistance` and
`SsrThickness` -- draws disabled with a line beneath it saying the
ray-traced equivalent is in use. That is a new registry hint,
`DisabledWhen(predicate, note)`, beside `OnlyWhen`: hidden means "does not
apply in this mode", disabled means "applies, and something else has
taken it over" -- the profile keeps its value, and turning the ray option
off hands it back. The predicate consults the *resolved* flags, so a
profile authored on an RTX machine and opened on OpenGL shows its SSAO
row live, which is what runs there.

### The checks

`check_ssr.py`: 7af's exactness law -- SSR on under sky K equals SSR off
under a sky of the block's colour -- holds for the traced form to the same
bound, because the block is emissive and the simplified hit shade is exact
for emissive; and a second scene, the block moved above the top edge of
the frame with its reflection still on the floor inside it, where SSR has
nothing to walk to and shows the sky and the traced form shows the block.
The row the reflection lands on is *derived* from the fixture's camera and
the block's mirror image, not found in the frame, so the claim cannot be
circular. `check_ssao.py`: on the box fixture under RTAO the seam darkens,
the open floor holds, the brick wall holds at one, and the jitter-phase
claim from 7an holds across the eight frames.

Numbers at landing (Release, RTX 5070 Ti Laptop): traced reflection vs a
sky of the block's colour **0.01 levels**; off-screen block, reflection
spot against empty floor **+109.85 traced, -0.10 screen-space** (row 788 of
1080 derived); RTAO seam **15.17**, open floor 0.000, brick wall p10
**1.000** (worst 0.987), jitter-phase p10 1.000 and 0.04 levels of drift.
Falsified three ways: the reflection ray along `-direction` (208 levels off
the law, off-screen gain 0.00); the AO rays at zero length (seam 0.00);
and the normal rule back to "written wherever there is one" (wall p10
0.972 -- the defect above, reproduced on demand).

### What stays stated

Hit shading is Lambert plus emissive, one shadow ray toward the sun and
none toward local lights, no normal map, no parallax, no occlusion; a
reflection of a lit brick wall shows the wall's colour and its shadow, not
its bump. Rough surfaces keep the probe (the mirror weight is zero from
roughness 0.6). No penumbra still. RTAO is twelve rays through SSAO's blur
with no temporal accumulation, so a wide `AoRadius` on a fine occluder
shows the twelve. And the two ray options ride on Shadows: with shadows
off there is no structure and neither runs, by design and by the panel.

---

## 7ap. Terrain (8.4): a heightfield that is a mesh source, and nothing the renderer has to learn

The roadmap's price for 8.4 was a decision, not work: the tree carried a
false start, `experiments/terrain/Chunk`, which generated one entity per
visible cube *face* -- thousands per chunk, each with a transform, a tag, a
relationship and a colour, each drawn as its own quad -- and the row said
it had to be deleted first so nobody built on it. **The owner's call at
landing was to keep it, cut off.** It already was: `experiments/` is not
on any include path or in any target (its README says so), and the one
thread still tying it to the engine was `perlin_noise`, a header-only
vendored library that RageV linked and nothing included -- that link is
gone. The experiment stays as what it is, the shape to avoid, with the
README pointing here for the shape that was built instead.

### What terrain is here

**A heightfield, not voxels.** A regular grid of heights, one surface, no
overhangs, no caves. That is what a game at Godot's scope means by the
word -- Unity's, Godot's and Unreal's terrains are all heightfields -- and
it is the shape every other system already knows how to take: a
heightfield is a mesh with a rule for making it, a collider Jolt ships a
shape for, and a thing a ray can hit. Voxel worlds are a different engine
(streaming, meshing, a different physics story) and the experiment's
lesson is that they are not this one's business.

**One decision governs the design: terrain is a *source of meshes*, and
the renderer never learns the word.** Every chunk is an ordinary `Mesh` --
vertex buffer, index buffer, bounds, CPU positions, a BLAS on first use --
so batching, frustum culling, the shadow pass, the ray-traced shadows and
reflections, the probe choice, TAA's velocity, picking and the depth sort
all take it for free, exactly as they take a crate. `Renderer3D` is
unchanged by this section. What is added is a component, an asset, a
builder that turns heights into chunk meshes with LOD, and a walk in each
place the scene enumerates its meshes.

### The asset: `.rvterrain`, its own file, and why not a texture

The heights are an asset of their own -- `RVTR` v1: a resolution `R`
(2^n+1, 33 to 4097), then `R*R` unsigned 16-bit heights, row-major, 0 to
65535 spanning the component's height in metres; a small header reserves
room for stage 2's layer weights so the format does not turn over then.
`AssetType::Terrain`, `.rvterrain`, `TerrainSerializer` through the VFS
like every other reader, `Assets::Manager::GetTerrain` caching a
`TerrainData` (the CPU heights) by handle. Made by
`tools/scripts/make_terrain.py`, from a 16-bit PNG or from noise; an
importer inside the editor is a stage-2 nicety, not a stage-1 need.

Not a texture, for three reasons that each decide it alone. **Precision**:
a height needs 16 bits (a 100 m range in 8 bits is 39 cm steps, and a
slope of 39 cm terraces reads as terraces), and the cook policy of §7i
turns a `_height` map into BC4 -- rightly, for a parallax map, which is
what that suffix means -- so a heightmap named the obvious way would be
quantised on the way into a pak. **Access**: the terrain needs the samples
on the CPU, for the mesh, the collider and `HeightAt`, and a texture asset
is a GPU object; the pixel-readback path a texture would need is a second
loader for the same bytes. **Ownership**: stage 3 (sculpting) writes
heights back, and the thing it writes back to has to be a file the engine
owns the format of. The same reasoning made `.rmat`, `.rvpostprofile`,
`.rcurve` and `.rvlut` their own files rather than fields on something.

### The component, and where the terrain sits

`TerrainComponent { Terrain (asset handle), Size (metres per side, 256),
Height (metres at a full sample, 40), Material (handle, null = the
default), TextureScale (metres per texture repeat, 4), Collision (true) }`.
The terrain occupies `[-Size/2, Size/2]` in local X and Z and `[0,
Height]` in local Y, and the entity's transform places it -- **centred on
the origin, not cornered there**: an entity placed at (0,0,0) should have
the terrain around it, and every other primitive here is centred. Rotation
and non-uniform scale are legal and go through the world matrix like any
mesh's; the collider decomposes the same matrix.

`Material` is one ordinary `.rmat`, tiled: the vertices carry `uv = local
metres / TextureScale`, and the material's own `UvTransform` multiplies on
top as it does for every mesh. A layered material -- four `.rmat`s blended
by a weight map painted into the asset -- is stage 2, and is a *shader*,
not a change to any of this; the asset reserves the bytes for it. Stage 1
is deliberately "a mesh with a material", because that is what every
downstream system knows how to light.

### Chunks, LOD, and skirts

The heightfield is cut into **chunks of 64 quads** (a 513 terrain is 8x8
= 64 chunks; a 4097 one is 64x64 = 4096, and its bounds are its problem).
Each chunk is built at **four levels of detail** -- 64, 32, 16 and 8
quads a side, i.e. 65^2, 33^2, 17^2 and 9^2 vertices, sampling every 1st,
2nd, 4th and 8th height -- as four `Mesh` objects, up front, at load. A
513 terrain is 64 x (65^2 + 33^2 + 17^2 + 9^2) x 32 bytes ~ 11 MB of
vertices and about the same again in indices and CPU positions; a 1025 is
four times that. Stated, and acceptable for a terrain that is a mesh
source: the alternative -- one grid, heights fetched in the vertex shader,
a clipmap that follows the camera -- draws no meshes, so it would need its
own shadow path, its own BLAS refit and its own picking, and that is the
"different piece of code" the experiment's README already warned about.

**Which level draws is chosen per chunk, per frame, once**, from the
camera's distance to the chunk's centre: level = clamp(floor(log2(d /
(4 x chunk width))) + 1, 0, 3), so a chunk within four widths is full and
each doubling of distance halves it -- 128, 256 and 512 m on a 256 m
terrain of 513 samples, where a chunk is 32 m. (The first draft said two
widths; the first frame showed a skyline stepping between levels sixty
metres from the camera, which is where the eye rests.) Chosen in `Terrain::SelectLod(camera)` at
the top of the frame and *read* by every consumer -- the scene pass, the
shadow casters, the ray-instance list, the picker -- so the geometry that
casts a shadow is the geometry that receives it and the geometry a ray
hits is the geometry drawn. A chunk outside the frustum still gets a level
(it casts), it just is not drawn.

**Cracks between levels are hidden by skirts**, not by stitching. Two
neighbouring chunks at different levels meet along an edge where one has
twice the vertices of the other; the coarse edge cuts corners the fine
edge follows, and the difference is a sliver of background. Stitching
(index buffers per neighbour combination) is exact and combinatorial;
a skirt -- the chunk's edge vertices duplicated a little way *down*, joined
to the edge by a strip of triangles -- is one rule and hides the sliver
from every angle a camera above the ground can take. Each chunk's skirt
hangs by half the chunk's own height range plus a fraction of the
terrain's, wound both ways so it is not culled from either side, carrying
the edge's normal so it lights as the surface does -- and only on the
edges a neighbouring chunk shares: the first frame of the cliff fixture
showed the terrain's outer rim wearing a curtain, which is a skirt on an
edge that meets nothing and can crack against nothing. What is *not* done:
geomorphing. A chunk pops when its level changes; TAA softens it and the
distance rule keeps it small on screen. Stated as the limit it is.

**Skirts are drawn only while the camera is above the ground** (added
2026-08-17, after the owner dropped a terrain into the demo scene and looked
under it: "I can see the separators for each block"). Seen from *under* the
heightfield the surface's back faces are culled -- as every engine culls
them; Unity and Unreal are invisible from below -- and what remains is every
chunk's skirt, wound both ways on purpose, hanging in the sky as a wall along
each shared edge: the separators. The skirt exists to fill a crack, and a
crack is only ever seen from above the surface, because a skirt is a vertical
drop from an edge whose height *is* the ground at that (x, z): everything
below the edge is inside the ground, and a viewer above the surface reaches
it only through a gap. So the rule is exact and cheap: `SelectLod` transforms
the camera into terrain space and compares it with `HeightAt` at its own
(x, z) -- clamped to the extent, so a camera off the rim compares with the
rim nearest it -- and while the camera is *under*, every chunk draws **only
its surface triangles**. Not a shader branch and not a second mesh: the
builder emits the surface's indices before the skirts', reports where they
end (`BuildChunkGeometry` returns the count, `Chunk::SurfaceIndices` keeps
it per level), and `Renderer3D::DrawLayeredMesh` takes an index count --
the renderer still never learns the word terrain; it draws the first N
indices of a mesh. The shadow pass and the acceleration structures keep the
whole mesh: a skirt is inside the ground and casts nothing the surface does
not, and a shadow map with cracks in its casters leaks light through them.
What a viewer under the ground sees now: sky through the surface, and the
front faces of any hill that rises into their view once the ray is above
ground -- with hairline cracks at those hills' level seams, since the skirts
are off; the stated trade, since it is the view from inside a hill. The
approximation is the camera off the footprint and *below the rim but above
the surface it looks at*, e.g. standing beside a plateau: the rule (nearest
rim height) says "under" and the far slopes lose their skirts -- hairlines,
not walls -- and the paper-thin rim itself is what it always was. Checked by
`check_terrain.py` claim 6 (the `under` fixture: a camera three metres under
the ridge's plain, looking away from the ridge and up -- every pixel is sky,
where before the fix the seams' skirts crossed the frame) and by scenetest
(under -> the surface count, above -> the whole mesh, on and off the rim).

**Normals** are central differences of the heightfield in metres, baked
per vertex; the material's normal map lands on top through the same
derivative tangent frame every mesh uses. **Triangulation** matches Jolt's
heightfield exactly -- the diagonal from (x, z) to (x+1, z+1), the two
triangles (x,z)(x,z+1)(x+1,z+1) and (x,z)(x+1,z+1)(x+1,z) -- so the
surface a body rests on is the surface drawn, to the triangle, and
`Terrain::HeightAt(x, z)` interpolates over the same split; a check drops
a sphere and asks that it rest at `HeightAt + radius`.

### The physics: Jolt's own shape

Jolt has `HeightFieldShape` for exactly this, and it is used: a static
body per terrain entity with `Collision` on -- offset (-Size/2, 0,
-Size/2), scale (Size/Q, Height, Size/Q) over the heights as floats in
[0, 1], the entity's world position, rotation and scale folded in the way
every collider's are. **No RigidBody or Collider component is needed or
consulted**: the terrain *is* its collider, static by nature, and a
RigidBody on the same entity is ignored with a warning rather than
honoured, because a terrain that falls is not a thing anyone meant.
Raycasts hit it and report the terrain entity; contact events name it.
The debug overlay (F3) draws nothing for it in stage 1 -- the terrain is
visible, and its collider is its surface -- and says so here.

### What each place that walks meshes now also walks

The scene enumerates its meshes in five places -- `OnRender` (draw),
`RenderShadows` (the map casters, and the ray-instance list), the picker,
and the asset preloader's want-list -- and each gains the terrain walk
through one helper: `Scene::ForEachTerrainChunk(entity, fn)` hands over
`(mesh at the chunk's selected level, the entity's world, chunk bounds)`.
One helper so the five cannot disagree about which level a chunk is at.

### The checks

`scenetest`: the serializer round-trips (bytes and heights); `HeightAt`
returns the corner heights at the corners and the triangle-exact
interpolation inside a quad on both sides of the diagonal; a chunk mesh at
each level has the vertex and index counts arithmetic predicts, every
skirt vertex sits below the lowest surface vertex of its edge, and a flat
terrain's normals are all `+Y`; the level rule gives 0 near and 3 far and
the same answer for two chunks at the same distance; a physics world built
from a terrain scene has a body, a ray from above hits it at `HeightAt`,
and a sphere dropped on it comes to rest at `HeightAt + radius`.

`check_terrain.py`: a scene with a terrain from a *known* heightmap (a
ridge along one axis) rendered on both backends: the ridge's silhouette
row is derived from the camera and the heights, not found in the frame; a
far chunk boundary at a level change shows no background pixels along the
seam (the skirt claim), and disabling skirts shows them (the
falsification); with `--raytracing=on` the ridge shadows the plain on the
far side. Falsified further by mis-triangulating (the resting sphere sinks
or floats), by flipping the level rule (near chunks coarse: the silhouette
row moves), and by an off-by-one in the row-major read (the ridge turns
ninety degrees).

### Numbers at landing, and what the fixtures taught

`scenetest` +53 (1681 on Vulkan under validation, 1641 on OpenGL): the
serializer round-trips 33^2 samples in 32 + 2178 bytes and refuses a wrong
magic, a truncation and a 34-grid; the corner quad samples 0 and 0.5 where
bilinear would say 0.0625 and 0.5625; a 129 grid is 2x2 chunks of 64 whose
corner chunk wears exactly two skirts of 65 (the count is arithmetic:
65^2 + 2x65 vertices, 6x64^2 + 2x12x64 indices), every skirt vertex its
edge dropped by 2.7 m; the ball rests at 5.50 +- 0.05 on a 5 m terrain,
the ray and `HeightAt` agree on the ramp to the centimetre, and a click
lands on the surface. `check_terrain.py`: the ridge meets the sky at row
90 on both backends where the crest projects to 90.1; its shadow darkens
the plain by **185.0 levels under maps and 185.0 under rays** (28.7 vs
213.7); the cliff shows **0 holes** with skirts and **253 without**.
Falsified three ways: skirts off (the 253); the row-major read swapped
(the ridge turns ninety degrees -- row 0, shadow 2.7); and the unit claims
each pin one thing the others do not.

Three findings on the way. **The level rule** was two chunk widths in the
first draft and is four: the first frame showed the skyline stepping
between levels sixty metres out. **Skirts on the outer edge** were a
curtain hanging off the world's rim in the cliff's first frame; a skirt
belongs only on an edge a neighbour shares. And **the crack fixture had to
be designed twice**: white noise per sample makes cracks and then fills
them -- the surface right behind a seam dips as often as not, so a ray
through the crack exits and hits the next bump, terrain-coloured, and the
skirts-off build measured *the same* 196 stray sky pixels as the skirts-on
one (sky between spikes at the silhouette, not holes). Noise per *column*
-- ridges running away from the camera -- keeps the seam rough and the
rise behind it smooth, so a ray through the crack stays under the surface
to the sky, and the two builds separate 0 from 253. A fixture that
cannot tell the broken build from the working one has not measured
anything yet.

### What stays stated

One material, no layers, no painting -- stage 2 (four materials blended
by a weight map stored in the asset, the same lighting includes: **built,
7aq**) and stage 3 (sculpt and paint in the editor, undo through the
command stack, written back to the asset). **The owner
set stage 3's shape at landing: a brush the user draws the ground with
(built, 7ar)**
-- size, strength, falloff; raise, lower, smooth, flatten under a dragged
cursor, the same brush painting layer weights later -- **not a library of
preset shapes stamped onto the grid.** A stamp is a shortcut for the one
terrain its author imagined; a brush is what every terrain editor since
Bryce has actually been, and the thing that turns a heightmap import into
an authoring tool. No
holes (caves, tunnels) -- a heightfield has none. No streaming: the whole
terrain's chunk meshes are built at load and stay resident, which caps a
practical terrain around 2049 (~180 MB of geometry) until a paging story
exists; the format allows 4097. LOD pops, softened by TAA. The skirts
add ~5% geometry per chunk at level 0 and two thirds of it at level 3
(48n against 6n^2 indices), and are drawn only while the camera is above the
ground (the paragraph above); from under it the terrain is invisible but for
the front faces of hills the eye ray rises into, with hairline seams on
those. `HeightAt`
is not yet a script call. And the
first draft's memory of the experiment stays true: one entity, one asset,
sixty-four meshes -- never one entity per face.

---

## 7aq. Terrain stage 2: a layered material, which is one fork in the lit shader and nothing new to light

Stage 1 (7ap) drew a terrain with one `.rmat` tiled over all of it, and
said the layered material was "a shader, not a change to any of this".
That held. What stage 2 adds is a way for the *surface* of a chunk to be
made of up to four materials in proportions painted per sample, and it
touches exactly the part of the lit shader that decides what the surface
is -- not one line of the part that lights it.

### What is decided, and by what

**Four layers, each an ordinary `.rmat`, blended by weights that live in
the `.rvterrain`.** The header word 7ap reserved is used: `layers` is 0
(stage-1 files, and a terrain nobody has painted) or 4, and when it is 4
the heights are followed by `R x R x 4` bytes -- one RGBA8 weight per
sample, row-major, the *same grid as the heights*. Not a texture asset,
for 7ap's own three reasons read again: the brush of stage 3 writes these
bytes back and needs them on the CPU, and the file it writes back to has
to be one the engine owns; a `.png` in the project would be cooked to a
block format on the way in and re-quantised on the way back. Not a
separate resolution either -- Unity's control map is one, but one grid
means one brush, one `Sample`, one file, and a `reserved` word is still
there the day someone paints finer than they sculpt. The sum of the four
weights need not be 255: the shader normalises, so a half-painted map is
not a darker one, and where the sum is zero -- unpainted, or a stage-1
file -- **layer 0 has weight 1**. That last rule is what makes a stage-1
scene look, to the pixel, as it did: layer 0 *is* the `Material` the
component already had, and the serialized key stays `Material` (its label
is now "Layer 0"); `Layer1`..`Layer3` are new keys. A layer left empty is
inactive and its weight is ignored in the normalisation; layer 0 left empty
is the renderer's default, as it was.

**The renderer learns "layered material", not "terrain".** `LayeredMaterial`
sits beside `Material` in `Renderer/Material.h`: four `Ref<Material>`
layers, a weight texture, and `WeightUv` -- how the mesh's own texture
coordinate reaches the weight map. It binds as **set 1 of a third lit
pipeline** whose fragment stage is `pbr_fragment.glsl` compiled with
`RV_LAYERED`, and whose vertex stage is the static one, factored into
`include/static_vertex.glsl` so it is not a third copy. On the bound path
set 1 is a uniform block of the four layers' scalars and thirteen samplers
-- the weights, and each layer's base colour, normal and roughness maps;
on the bindless path it is the same block with the thirteen heap slots
inside it, and no samplers, the maps read through the heap like every
other material's. One block layout, both paths, `LayeredParams` mirrored
by hand and size-asserted like `MaterialParams` is. The draw itself is
`Renderer3D::DrawLayeredMesh(mesh, world, layered, probe, previous)`, a
third kind beside static and skinned; the sort key, the pipeline bind and
the run loop each grew the one branch that kind needs. The terrain's
`Renderer/Terrain` owns the `LayeredMaterial` and the weight texture
(built in `Create` from the asset's bytes, a 1x1 red when unpainted, so
"no weights" and "layer 0 everywhere" are the same texture), and refreshes
the four layers from the component's handles **once per terrain per
frame** in `Scene::PrepareTerrains` -- the walk that also selects the
levels -- not once per chunk per consumer, which was four hash lookups
times sixty-four chunks times every walk.

**Three maps per layer, and why exactly three.** OpenGL guarantees sixteen
texture units per fragment stage and every desktop driver gives
thirty-two; the shared set 0 already spends sixteen (environment,
irradiance, BRDF, the reflection trace, four cascades, four spots, four
point cubes). Four layers of base colour, normal and roughness plus the
weights is thirteen -- twenty-nine of thirty-two -- and four layers of a
fourth map would be thirty-three. So on the *bound* path a layer's
occlusion, metallic, specular, emissive and height maps are not read; its
scalars are (metallic, roughness, occlusion, specular, emissive colour,
base colour, tiling). And the *bindless* path, which could read all eight
per layer for free, reads the same three -- because the two paths are
compared pixel for pixel (7al) and a terrain that gained ambient occlusion
when `--bindless=on` would be a difference that is not the feature's.
Stated as the limit it is: ground materials from the texture libraries
ship AO and height maps, and this stage ignores both. If it ever matters,
the way through is texture arrays per map (one `sampler2DArray` of four
slices per kind, which is what Unity and Unreal do) at the cost of
resampling four independent textures to one size and format on load, and
that is a stage of its own.

**The surface, once, in one function.** `pbr_fragment.glsl`'s `main` used
to sample the material's maps inline; the block is now `SampleSurface`,
returning a struct -- base colour, metallic, roughness, occlusion,
specular, the shading normal, emissive -- and *that* is what forks under
`RV_LAYERED`. The single-material body is the code that was there. The
layered body reads the weights at `v_TexCoord * WeightUv.xy + WeightUv.zw`,
zeroes the inactive layers, normalises, and for each of the four layers
with a weight above zero samples its three maps at its own tiled
coordinate and accumulates: base colour, roughness, metallic and the rest
by weight, the normal as the weighted sum of the four perturbed normals
renormalised. `textureGrad`, not `texture`, inside the weight test: the
test is a per-fragment branch on a value read from a texture, and an
implicit derivative inside divergent control flow is undefined -- so the
coordinate's derivatives are taken once outside, and each layer scales
them by its own tiling. The tangent frame is built per layer, outside the
weight branch for the same reason, because a layer's `UvTransform` can be
non-uniform or mirrored and the frame is a function of the transformed
coordinate; four derivative pairs cost nothing. No
parallax on layers (no height map is bound), no triplanar (a steep face
stretches its texture, as it does on any mesh). The lighting below the
struct -- clustered lights, shadows by map or by ray, the probe, the SSR
mix, RTAO through the normal attachment -- does not know which body
filled it, which is the whole point of the fork being where it is.

**Weight coordinate arithmetic.** The chunk vertices carry `uv = local
metres / TextureScale` (7ap), and the terrain spans `[-Size/2, Size/2]`;
the weight map has `R` texels a side whose centres sit at `(i + 0.5) / R`
while sample `i` sits at `i / (R - 1)` of the span. So the map is read at
`uv * (TextureScale / Size) * (R - 1) / R + 0.5` -- the offset is exactly
one half in both axes, and the scale carries the texel-centre correction --
which puts every sample's weight under its own vertex rather than half a
texel to one side, a shift the eye reads as the paint sliding uphill.
`Terrain::WeightUvFor(dimensions, resolution)` is that expression as a
pure function, and a check asks it for texel centres 0 and R - 1. The
weight sampler clamps to edge: repeat would blend the far rim's paint into
the near rim's last texel.

**Where the layers do not reach.** A ray-traced reflection (7ao) shades a
hit from the ray-instance table's one material record per instance, and a
terrain chunk's record is layer 0's: a terrain seen *in a mirror* shows
its base layer only. Stated, and small -- reflections of ground are
roughness-blurred ground. The shadow-depth pass, the picker and the
physics do not know a material exists and are untouched. `Material` grew
four const getters (its base colour, normal and roughness maps and its
sampler) so the layered material can read what it binds; nothing else on
it moved.

### The checks

`scenetest`: the serializer round-trips a 33-grid *with* weights (32 +
2178 + 4356 bytes) and the weights come back byte for byte; it refuses
`layers = 2` and a file truncated inside its weights; `WeightAt` reads the
interleaved bytes back by layer; `WeightUvFor` puts sample 0 and sample
R - 1 on texel centres 0 and R - 1 for a 129 grid at two texture scales;
`LayeredParams` is 368 bytes; a `LayeredMaterial`'s batch key changes when
one layer changes and does not when nothing did; a `TerrainComponent`
round-trips its four handles through the scene serializer.

`check_terrain.py` gains a fixture built for the claim: a flat 129 grid
under a camera looking straight down and a sun straight down, layer 0 a
flat red `.rmat` and layer 1 a flat blue one, the weight map painted red on
the left third, blue on the right third **at half intensity** (weights of
127, which the normalisation must lift to one), a linear blend across the
middle, and a strip along the top of the *right* third unpainted -- inside
the blue, so a claim about it cannot pass by measuring red that was
painted red. Four claims, the regions' columns and rows derived from the
camera: the left region red and the right blue (the dominant channel over
the other by two to one and a hundred levels, in display space -- the
tonemap lifts a 0.08 channel to 85); the two regions' brightness within
ten percent of each other (the normalisation claim -- unnormalised, the
right is half as bright); the middle column's red and blue within twenty
percent of each other (the blend); the unpainted strip red (the layer-0
rule, where the paint around it says blue). On both backends, and
on Vulkan with the heap on and off, so all three material paths the
layered shader has are the ones measured. Falsified by swapping the
weight channels in the shader (left and right trade places, the first
claim fails both ways) and by removing the normalisation (the brightness
claim fails, and the unpainted strip goes black).

### Numbers at landing

`scenetest` +21 (1702 on Vulkan under validation, 1662 on OpenGL), every
claim above as written. `check_terrain.py`'s fourth fixture on the three
material paths -- Vulkan with the heap, Vulkan without, OpenGL -- reads
left `[222, 87, 84]`, right `[89.5, 87, 219]`, middle `[196.6, 87, 189.2]`,
the unpainted patch inside the blue `[222, 87, 84]`: **identical on all
three**, which is the pixel parity 7al promised extended to a surface the
heap did not exist for. Falsified as planned: channels swapped, left and
right trade places on every path; normalisation dropped, the right reads
332 against the left's 393 and the unpainted patch goes to `[0, 0, 0]`.
The hills with the heap on and off differ by nothing (p99 0.0), and from
OpenGL by 0.34 levels on average. The painted hills under traced shadows,
reflections and occlusion together, under validation: zero lines. And the
stage-1 fixtures -- ridge row 90 vs 90.1, shadow 185.0 under maps and rays,
cliff 0 holes -- are unchanged by drawing through the layered pipeline,
which is the "layer 0 everywhere is a stage-1 terrain" rule measured.

One thing learned that is not about terrain: a shader backup with a generic
name (`pbr_fragment.bak`) left in the scratchpad by the *previous* session
was restored over the working file between two falsifications, and the
build drew one check run through a stage-1 fragment shader bound to a
layered set without a validation word about it -- the block was smaller
than the layout expected and the samplers went unwritten, and Vulkan said
nothing. The check caught it (every region red), the backup at a dated
name was right, and the rule is: diff a backup against the file before
restoring it, and never reuse a backup name across sessions.

### What stays stated

Three maps per layer, on both paths, for the reason above. Four layers,
because the weights are an RGBA8 texel. Weights at the heights'
resolution. No parallax and no triplanar on layers. Layer 0 only in a
traced reflection. The sample project has no grass, rock or snow textures,
so the hills' three layers are tints of its one soil; the shader does not
know that. Painting is stage 3, and its shape is set (7ap): a brush the
user draws with, and now the same brush paints weights.

---

## 7ar. Terrain stage 3: the brush -- the user draws the ground, and every stroke is one thing that can be undone

The owner set the shape of this stage before it was built (7ap): **a brush,
not stamps.** Not a menu of "hill" and "mountain" dropped onto the grid,
but a circle under the cursor with a size, a strength and a hardness that
raises, lowers, smooths and flattens the heights as the mouse drags, and
paints the four layers of 7aq with the same motion. Everything below is
what that costs to do properly, and where the cost was put.

### What is decided, and by what

**The brush is a function of the data, and lives with the data.**
`Asset/TerrainBrush` is a settings struct -- `Mode` (Raise, Smooth,
Flatten, Paint), `Radius` in metres, `Strength` 0..1, `Hardness` 0..1,
`Layer` 0..3, `Invert` -- and one pure `Apply(TerrainData&, size, height,
localX, localZ, flattenTarget, dt)` that edits the samples under the circle
and returns the inclusive sample rectangle it touched. Pure, headless, and
tested as arithmetic: a raise on a flat grid rises by the rate, a lower is
its mirror, a smooth takes a spike down and its neighbours up, a flatten
converges on its target, a paint moves weight and nothing else. The
kernel: `t = d / r`, weight 1 inside `Hardness * r`, then `1 -
smoothstep((t - Hardness) / (1 - Hardness))` out to the rim, zero beyond --
so hardness 0 is a soft cone from the centre and hardness 1 a hard disc.
The rates are relative to the terrain's `Height` and to time, not to
frames: at strength 1 a full-weight sample rises a quarter of `Height` per
second (a 40 m terrain climbs 10 m/s under the centre of the brush), a
smooth or a flatten closes an eighth of the gap per sixtieth of a second
at most, a paint the same. Frame-rate independent by construction, and the
numbers are the ones a check can ask for.

**Smooth is a 3x3 mean read before it is written; Flatten aims at the
height under the cursor when the button went down.** Both are what every
sculpting tool means by the words. **Paint replaces**: layer `L` moves
toward 255 by `a` and every other layer scales by `1 - a`, so a texel that
was all-something stays all-something and the shader's normalisation sees
a sum that neither grows nor collapses. And -- the rule that took a moment
-- **an unpainted texel is materialised as layer 0 before it is painted**:
the shader reads a zero sum as layer 0 (7aq), so a fresh terrain is
`(0,0,0,0)` everywhere, and painting layer 1 into that with the faintest
touch would give `(0, a, 0, 0)` and normalise to *all* layer 1. Written as
`(255,0,0,0)` first, the touch blends as the eye expects. Shift inverts:
lower under Raise, erase under Paint (the layer's weight scales down, and
the sum going to zero is layer 0 again by the same rule).

**One stroke is one command.** A press starts a stroke, every frame the
button is held applies one step at the cursor's point on the *current*
surface, a release ends it -- and the whole of it is one
`TerrainStrokeCommand` on the stack, so Ctrl+Z takes back the drag and not
its last frame. The command holds the touched rectangle and its samples
before and after (heights for a sculpt, weights for a paint, never both),
recorded by a `StrokeRecorder` that grows its rectangle as the stroke
travels and copies each newly covered sample *before* the step that would
change it. Execute writes the after, Undo the before, both through the
same `Terrain::ApplyRegion` the live stroke uses, so a redo draws exactly
what the drag drew. `TouchesScene` is true: the stroke edits an asset the
save has to write, and the unsaved mark's position arithmetic then works
for terrain edits as it does for everything else -- undo back to the save
point and the mark goes out.

**Where the heights live while they are being edited.** There are two
copies of a terrain's `TerrainData` -- the asset manager's cache, which
`Terrain::Create` copies from, and the `Terrain` runtime's own -- and the
manager's is authoritative: `Assets::Manager::EditTerrain(handle)` hands
out the mutable one and marks it dirty; `Terrain::ApplyRegion(data, rect)`
copies the rectangle into the runtime's copy and rebuilds. Editing only the
runtime's copy would have been the bug where changing `Size` (which
replaces the runtime from the cache) silently threw the sculpt away.

**Per-chunk, per-level, lazily.** `Terrain::Resolve` replaces the whole
object on a dimension change and that stays; a stroke must not. So
`Terrain::Invalidate(rect)` marks every level of every chunk overlapping
the rectangle *grown by one sample* (the normals are central differences,
so a sample's change moves the normal one sample either side) as stale,
and refreshes each chunk's bounds straight from the data (min and max
height over its samples plus the skirt drop -- no mesh needed, so culling
is right before any rebuild). `Terrain::SelectLod`, which already runs once
per frame before every consumer, rebuilds the *selected* level of any stale
chunk on its way past; the release rebuilds every stale level. During a
drag over four chunks that is four meshes a frame instead of sixteen, and
the level the camera sees is never stale for a frame it draws. A mesh
rebuild is a new `Mesh` (the old one's buffers go through the deletion
queue when the last frame that named them retires), and its BLAS is built
on first use as ever, so the ray-traced shadow of a hill follows the brush
a frame behind the hill. The weight texture is updated in place: a new RHI
verb, `RHITexture::UploadRegion(x, y, w, h, bytes)`, mip 0 layer 0 only --
`glTextureSubImage2D` on one backend, a staged `vkCmdCopyBufferToImage`
with an offset on the other -- so a paint stroke on a 4097 terrain uploads
its rows and not 64 MB. In place also means the bindless heap slot needs
no re-registration.

**Where the cursor is on the ground.** The click-to-select picker tests
the chunk meshes at level 0, and during a stroke those may be stale by
design. The brush therefore asks the *data*: `Terrain::Raycast(localOrigin,
localDirection)` clips the ray to the terrain's box, marches it in steps of
half a cell comparing the ray's height with `HeightAt`, and bisects the
first crossing to a millimetre. Exact against the surface being sculpted,
independent of what any mesh currently says, and cheap -- a 4097 grid is
eight thousand steps in the worst case. The editor transforms the mouse
ray into terrain space through the entity's world matrix, so a rotated or
scaled terrain sculpts under the cursor like an unrotated one.

**Every upload waits, and the number is stated.** Buffer and texture
uploads in this RHI stage through an immediate submission that waits for
the GPU -- that is how every asset loads. A stroke step over four chunks
is four meshes, each two uploads, plus one region upload: nine waits, a
few milliseconds on this machine. Acceptable for a tool held in a hand,
measured below, and the honest note is that a shared per-frame transfer
command buffer is the fix if it ever is not; that is a change to how
everything uploads and not to the brush.

**Written back on save, not on release.** A stroke marks the asset dirty;
the scene save writes every dirty terrain through `TerrainSerializer::Save`
(which already writes the paint) and re-indexes its `.meta` through a new
`Registry::Reindex(handle)`, one file's hash rather than the whole
project's rescan. Writing on release was considered and rejected: a
4097 terrain is 96 MB, and a stroke that ends in a hundred-millisecond
disk write is a stroke that stutters; the save is where the user expects
to wait. Undo past the save point dirties it again by the same position
arithmetic, and the next save writes the undone terrain.

**The tool, in the editor.** `Tools/TerrainBrushTool` holds the settings
and the stroke; the Terrain component's inspector block draws its controls
under the fields -- the four modes as buttons, size, strength, hardness,
and under Paint the four layers by their materials' names -- and the
viewport drives it: while a mode is chosen and a terrain is selected, a
plain left drag on that terrain sculpts (Alt+left still orbits, right still
flies), the click-to-select picker stands down over the terrain, and a ring
is drawn on the surface through the debug renderer at the brush's radius
with an inner ring at its hard core. `[` and `]` change the size. **Edit
mode only**: in Play the tool is inert and its block says so, because a
terrain is an asset and Play is for playing; the height field body is
built from the data at the next Play, so a sculpt made in edit mode is
what the ball rolls on. Jolt's `HeightFieldShape::SetHeights` exists for
the day someone wants to sculpt under a running simulation, and is not
used.

**A stroke by command line.** `--brush=mode,x,z,radius,strength,seconds[,layer]`
applies one stroke to the selected terrain (`--select` names it) at
terrain-local `(x, z)` for `seconds` of sixtieths, through the tool's own
begin/step/end, then saves the terrain asset and carries on. It is what
lets a check hold the brush: `check_terrain.py` runs the editor with it on
a fixture built for the purpose, then renders the *saved* asset with the
runtime and measures -- so the whole path from kernel to file to pixels is
under one claim, and the fixture is regenerated afterwards.

### The checks

`scenetest`: the kernel's weight is 1 at the centre, 0 at and beyond the
rim, monotone between, and equal at equal distances; a raise on a flat
grid lifts the centre by exactly `Strength * Height / 4 * dt` and nothing
outside the rim; Shift lowers by the same; a smooth on a spike lowers the
spike and lifts its eight neighbours; a flatten converges on the target
and stops there; a paint on an unpainted texel materialises layer 0 first
(`(255(1-a), 255a, 0, 0)`), a second layer replaces proportionally, an
erase scales down; the touched rectangle is exactly the samples the kernel
reached; the recorder's before is what the grid held before the first
step over each sample; a `TerrainStrokeCommand` executed and undone
through a real `Terrain` leaves `HeightAt` and the level-0 vertices where
they started and puts them back on redo; `Raycast` hits a flat terrain
where the ray meets the plane, hits a ridge from the side at its slope,
and misses when pointed away; `UploadRegion` beyond the texture is refused
and survivable.

`check_terrain.py` claim 5, on the `brush` fixture (the ridge's heights
under the ridge's camera, red layer 0, blue layer 1, unpainted): the
editor with `--brush=raise,-30,20,10,1,2` saves a bump 20 m tall on the
4 m plain in front of the ridge; the runtime renders it; the top of the
region that changed against the unsculpted frame lands on the row the
bump's peak projects to, within a few pixels; and `--brush=paint,...,1`
puts blue at the stroke's centre where the frame was red. Falsified by
inverting the raise (the region's top drops below the plain's row -- a
hole, not a hill) and by painting the wrong channel (the centre stays
red).

### What stays stated

Four modes; one brush shape (a disc with a hardness); no brush textures,
no clone or ramp tools; no symmetry -- **7as (designed the same day, not
yet built) adds the shapes, the patterns, and terrace, ramp, set-height and
erode.** Strength is a rate, so a slow machine
sculpts as fast as a quick one but a held frame is a held brush. The
heights are sixteen bits and round to nearest, so a held smooth or flatten
stops when an eighth of the remaining gap is under half a unit -- within
four units of its target, a fraction of a millimetre on a ten-metre
terrain, a quarter of one on a four-thousand-metre one. The paint's eight
bits would stall the same way -- at 4, where 4 x 7/8 rounds back to 4 --
and 4 is not 0 to the zero-sum rule, so a paint step that means to move
and would round to nothing moves one unit toward where it meant to go: an
airbrush held on one spot saturates, at its soft edge one unit a frame,
which is what every painting tool means by holding it, and a sculpt does
not creep the same way because a quarter-millimetre a frame at the rim
would be a hard edge in a minute. Uploads wait. Edit mode only. The write-back is on save. `HeightAt` is
still not a script call.

---

## 7as. Terrain stage 3b: brush varieties -- a shape, a pattern, and four more things a stroke can do

The owner's ask at 7ar's landing (2026-08-17): "different brush varieties
which can generate terrain in different patterns like Unity or Unreal".
Both engines' terrain tools decompose the same way, and so does this one:
**a brush is a kernel and an operation.** Unity has a library of brush
*masks* (any greyscale image, rotated, scattered) that every tool -- raise,
set height, smooth, stamp, paint, noise, terrace, erosion -- reads its
weight from; Unreal has brush *shapes* (circle with a falloff, an alpha
texture, a texture tiled over the landscape as a *pattern*) that every tool
-- sculpt, smooth, flatten, ramp, erosion, noise -- reads. 7ar's brush was
one shape (a disc with a hardness), no pattern, four operations. 7as makes
the kernel `shape x pattern` and adds four operations, and **nothing else
in the stroke changes**: the recorder, the one-command-per-stroke, the lazy
per-chunk rebuild, the write-back on save, `--brush` and the checks all
carry over, because a variety is a different weight per sample or a
different delta per sample and neither touches how a stroke is held.

### The kernel: shape times pattern

**Shape** is what the brush looks like from above, in *brush space*: `Disc`
(7ar's radial rule, unchanged) or `Mask` -- a `BrushMask`, a square
greyscale image (`Size x Size` floats in [0, 1], row-major from the top
left) laid over the brush's square `[-Radius, Radius]^2`, **rotated** by
`Angle` (and, with `FollowStroke`, by the direction of the last movement
too, so a streak brush streaks along the drag), sampled bilinearly, zero
outside the square. Hardness is the disc's and is ignored for a mask: the
image *is* its fall-off. `Weight(dx, dz, direction)` is the new question --
the weight at an offset in metres -- and `Weight(distance)` stays as the
disc's radial rule, which the ring, the ramp's cross-section and the old
checks still ask. A mask's footprint is the rotated square's box: the disc's
grown by root two, whatever the angle, because it is cheaper to record a
few more samples than to be clever about a rotating rectangle.

**Pattern** is a field over the *terrain*, in terrain-local metres, that
multiplies the weight wherever the brush touches: `None` (1), `Noise` (a
value-noise fBm, three octaves, in [0, 1], at `PatternScale` metres per
repeat, seeded), or `Tiled` (a mask again, wrapped at that scale). This is
Unreal's pattern brush and Unity's Noise tool in one rule: a raise through
a noise pattern raises where the field is white and leaves the valleys
between, which is what "generates terrain in a pattern" means, and a paint
through a tiled stripe mask paints stripes across the world -- the pattern
lives on the ground, so two strokes over the same place agree, and a
stroke that walks across the terrain reveals it rather than dragging it
along. `PatternAt(localX, localZ, seed)` is pure. The noise is the engine's
own hash-based value noise (`TerrainBrush::Noise`), not `<random>`, so
a seed means the same field on every machine and a check can name a value.

Per sample, then: `w = Weight(offset, direction) * PatternAt(sample)`, and
every operation, old and new, reads `w`.

**The masks are files, and every one of them is a landform or a ground
texture** -- the owner's correction while this was being built: brush shapes
have to be things a terrain is made of, not decorative stamps.
`RageVEditor/assets/brushes/*.png`, ten of them generated by
`tools/scripts/make_brushes.py` (numpy, seeded, 128 x 128, committed).
Shapes, for Raise, Lower and Flatten: **`dome`** (a raised cosine -- the
workhorse hill, softer than the disc), **`mountain`** (a peak roughened by
ridged fBm with a few warped spurs on its flanks, so one press has flanks
and gullies already), **`ridge`** (a crest along the brush's x, tapering
across and rounded at the ends -- dragged it grows a range, and its clean
core is what the check rotates), **`mesa`** (a flat top with a short steep
fall, its sides wandering a little), **`crater`** (a rim with nothing
inside: crater, caldera, berm; lowered, a moat or a quarry bench),
**`pad`** (a hard rounded rectangle, flat to its edge -- what Flatten and
Set Height want for a road, a yard or a foundation). Patterns, tiled over
the ground under any mode: **`erosion`** (ridged noise squeezed along one
axis -- gullies down a slope, the mask worth Follow stroke), **`rock`**
(fine fBm for roughening a surface that reads too smooth), **`veins`** (the
owner's ask: a connected network of ridgelines -- the seams of a wrapped
Voronoi diagram, so raising through it grows *linked* mountain spines with
basins between them, which a field of separate lumps never gives), and
**`dunes`** (one clean raised-cosine crest per tile, constant along z: wind
ridges, and the pattern whose crest and trough a check can point at). The editor
enumerates the folder through the VFS on first use (`BrushLibrary`, in the
tool) and decodes each with stb_image on demand -- `BrushMask::Decode` reads
the red channel -- so a user drops a PNG in the folder and has a brush; the
same library serves the shape combo and the pattern combo, which is one
list to maintain and one place a new mask appears twice. Not assets: a
brush mask is a tool of the editor, not content of the project, and the
project's registry should not learn of it any more than of the gizmo atlas.

### The four operations

Every one is a `w`-weighted blend toward a target, the 7ar rule (an eighth
of the gap per sixtieth at strength 1), except erosion, which is a rate of
droplets. `Op` grows to eight and the mode row in the inspector becomes a
combo -- which is also the fix for 7ar's papercut, the four buttons that
truncated to "Ra Sm Fla Pai" in a narrow panel; eight would not have fitted
at any width.

- **Terrace**: the target is the sample's height rounded to the nearest of
  `TerraceSteps` levels across `Height`. Held, it makes plateaus and risers;
  the risers are as steep as the blend has had time to make them.
- **Ramp**: two points -- where the stroke began (`Stroke::Start`, with the
  height there, 7ar's flatten target under a new name) and where the cursor
  is now (its *current* height, sampled by the step) -- and a straight ramp
  between them, `2 * Radius` wide, lerping the two heights along the line
  and using the disc's radial rule *across* it (distance to the segment,
  round-capped) as the weight. Blended toward, so it forms as the drag
  goes; the footprint is the segment's box grown by the radius, which is
  why `Footprint` and `Apply` take a `Stroke` now and the 7ar signatures
  stay as wrappers that build one.
- **Set Height**: toward `TargetHeight` metres, an absolute. Shift+click
  samples the height under the cursor into it (Unity's gesture); the stroke
  itself ignores Shift.
- **Erode**: hydraulic, the droplet algorithm every terrain tool since
  Beyer's thesis has shipped -- a droplet lands (rejection-sampled by the
  kernel, so the brush's shape is where it rains), rolls downhill with a
  little inertia, picks up sediment when it speeds up and drops it when it
  slows or climbs, evaporates, and dies at the footprint's edge or after
  thirty steps. `kDropletsPerSecond = 1500` at strength 1, in a float copy
  of the footprint, rounded back to sixteen bits at the step's end;
  erosion and deposition are weighted by `w` at the spot so the brush's
  edge is soft. Seeded from `Stroke::Seed`, which the tool advances every
  step, so a scripted stroke is the same stroke twice and a check can hold
  it: material leaves the crest and arrives at the foot, the sum inside
  the footprint falls by only what evaporation and the rim let go.
  Confined to the footprint on purpose -- a droplet that would run out of
  it stops, because the recorder covers the footprint and nothing else,
  and a stroke must undo whole.

`Invert` (Shift) still means what 7ar said for raise and paint; the four
new operations ignore it, except that Set Height reads it as "sample".

### The tool, the inspector, the script

`TerrainBrushTool` gains the stroke's context: the start point and its
height, the direction of the last movement (from consecutive hit points
further apart than a hair; the direction of a still cursor is the last
one), a seed counter, and the library. The overlay draws the rotated
square for a mask shape -- it turns with the angle and, following, with the
drag -- the ring for the disc, and the ramp's centreline while a ramp is
being drawn. The inspector: **Sculpt** (an accent toggle -- with a combo
there is no "click the lit button to stop"), **Mode**, Size / Strength /
Hardness (hardness greyed under a mask), **Shape** (Disc, then the
library), **Angle** and **Follow stroke** under a mask, **Pattern** (None,
Noise, then the library) and **Scale** under one, and per mode: the layer
buttons (Paint), Steps (Terrace), Height (Set Height, with the Shift+click
hint), a one-line hint (Ramp, Erode). `--brush=` keeps its six positional
fields and the optional seventh, and takes `key=value` tokens after them:
`shape=`, `angle=` (degrees), `follow=1`, `pattern=noise|<mask>`, `scale=`,
`steps=`, `height=`, `to=x:z` (the ramp's end -- the scripted stroke
begins at (x, z) and holds every step at `to`, a press-drag-hold),
`seed=`. Modes: the six of 7ar plus `terrace`, `ramp`, `setheight`,
`erode`.

### The checks

`scenetest`: a hand-built mask (left half 1, right half 0) sampled
bilinearly, wrapped and unwrapped, and through `Weight(dx, dz)` -- full on
the left, none on the right, and the reverse at half a turn, and following
the direction; the disc's radial rule unchanged for `Shape::Disc`; the
noise in [0, 1], the same at the same seed and place, different at another
seed; the tiled pattern wrapping; a masked footprint is the disc's grown by
root two; terrace pulls a mid-slope toward the nearest of its steps and
leaves a sample already on one alone; set height converges on the target
in metres; a ramp from a low corner to a high one lerps along the line, is
flat across it inside the core, and leaves the far field alone; erode on a
cone lowers the apex and raises the foot, conserves within the footprint
to a stated tolerance, and is bit-identical for the same seed and different
for another; the 7ar wrappers still equal the `Stroke` calls. `check_
terrain.py` claim 7 on a new `stamp` fixture (a flat unpainted 129 grid,
red layer 0 and blue layer 1, the layers fixture's straight-down camera and
sun): the *editor* paints layer 1 with `shape=ridge` at angle 0 -- the
crest lies along x, so the pixel 0.7 of a radius along **x** reads blue and
the one 0.7 along **z** stays red (the mask reads 0.95 and 0.03 there,
stated from the image); at `angle=90` the two swap, which no radial kernel
can do; and a paint through `pattern=dunes` at `scale=16` puts blue on the
crest at x = 0 and leaves red in the trough at x = 8 -- one crest per tile,
derived from the tiling. Falsified by ignoring the angle (90 degrees reads
as 0), and by sampling the pattern in brush space instead of terrain space
(the crest follows the stroke and the trough pixel goes blue).

### What stays stated

No clone/stamp-from-elsewhere, no thermal erosion, no symmetry, no scatter
or spacing controls; one mask per shape and one per pattern. The noise is
the brush's own, unrelated to `make_terrain.py`'s. Erosion is confined to
the footprint (a droplet that would leave stops -- a large brush erodes as a
landscape does, a small one as a puddle). Masks are the editor's, not the
project's; a packaged game never sees them.

---

## 7at. Global illumination (9.12): a screen-space pass in the profile, a ray in the shader, and one of them at a time

**DESIGNED AND WRITTEN 2026-08-17. The OpenGL regression that held it back is
FOUND AND FIXED (below); the feature itself is still unverified -- no bleed
measurement, no RT GI run, no checks of its own -- so it is still not
committed.** Everything below is
the design, and it was built once, end to end: it compiles, the SSGI chain
runs, and its add measurably lifts a frame (173,582 pixels changed on the
corner fixture). Two bugs stopped it, both mine, both now fixed.

**The first: sharing SSAO's blur.** Moving *its* depth from green into alpha so
one blur could serve both left the neighbour samples typed `vec2` reading
`.a`; the shader failed to compile, `PostProcess` never became ready, the
whole post chain was skipped, and **every frame rendered black**. SSGI has its
own `ssgi_blur.rvshader` now and SSAO's three shaders are untouched -- a copy
is the cheaper mistake than editing a pass three shipped features depend on.

**The second: one uniform block, declared two ways.** With the C++ half applied
`check_ssao.py` failed on OpenGL ("AmbientOcclusion: false changed the image,
max 64") and passed on Vulkan. The claim it broke is "off is off to the byte",
and the reason is worse than a wrong value: **two identical OpenGL runs of the
same scene differed by 67 levels.** The scene uniform block is *mirrored by
hand* in two shader files -- `include/pbr_fragment.glsl` and
`include/scene_vertex.glsl` -- and the struct's own comment in Renderer3D.cpp
says so. This added `vec4 GlobalIllumination` to the C++ struct and to the
fragment mirror **only**. Vulkan does not care: each stage's block is a view
over one buffer and a stage that declares fewer fields simply reads fewer. On
OpenGL the two stages are *linked into one program*, and a uniform block
declared two different ways in one program is ground the spec does not
define -- the linker resolves it as it likes, and what it resolved to varied
between runs. Adding the field to both mirrors makes OpenGL bit-identical run
to run again, and `check_ssao.py` green on both backends (scenetest also 1798
Vulkan / 1758 OpenGL, unchanged). **The rule, now written at the struct: a
field added to `SceneUniforms` must be added to every shader that mirrors the
block.** It is the same class of failure the struct's older comment warns
about for `PreviousViewProjection` -- "the two disagreeing is a picture that
is wrong rather than a build that fails" -- except that on OpenGL it is not
even consistently wrong.

The work is parked in `build/9.12-gi-wip/` (git-ignored, on the owner's
machine): `gi.patch` (settings, resolve, CLI, PostProcess entries, graph
passes, the lit-shader block, registry rows) plus the four new files it does
not carry -- `ssgi_compute`, `ssgi_blur`, `ssgi_apply` and `make_gi_scene.py`.
Apply with `git apply build/9.12-gi-wip/gi.patch` and copy the shaders back;
**the patch predates the uniform-block fix, so add `vec4 GlobalIllumination`
to `scene_vertex.glsl` as well after applying it.** Still unfinished: the `gi_corner` fixture's camera does not
frame the red wall (the bleed claim has never been measured), RT GI has never
been run, and there are no scenetest or check_gi.py claims yet.

The owner's ask (2026-08-17): "global illumination which will be a part of
post processing profile, and also ray traced illumination -- its option
should be available in rendering settings under ray tracing when ray tracing
is enabled, and when RT illumination is selected the global illumination gets
disabled/greyed out."

That is exactly the shape 7ao already built twice. **A screen-space effect
lives in the post profile; its traced twin lives in Render Settings under the
ray-tracing switch; the traced one takes over and the profile's row greys
with a note.** SSR/RT reflections and SSAO/RTAO are that pattern; this is the
third pair, and it reuses the machinery rather than inventing a third way:
`ResolveRayTracedGlobalIllumination`, `FieldHint::DisabledIf`, the same
"ray-traced equivalent in use" note, the same `--raygi=` override.

**What indirect light *is* here.** Both forms answer one question -- how much
light arrives at this pixel from the *scene* rather than from a light or the
sky -- and both add it as diffuse. Neither is a full light transport: one
bounce, no specular indirect (SSR and RT reflections are that), no
multi-bounce, no infinite light.

### The screen-space form: SSGI in the profile

`PostSettings::GlobalIllumination`, with `GiIntensity` and `GiRadius`. The
chain is SSAO's chain with a different first pass and a different last one,
which is deliberate: the shape is known to work, the blur is *literally the
same shader*, and the half-resolution decision, the reconstruction of
position and normal, and the projection scales are all already carried in
`PostParams`.

1. **`ssgi_compute`**, half resolution. Position and normal come from the
   depth buffer and the surface attachment exactly as SSAO's do (7ac, 7ae --
   the written normal where it agrees with depth, reconstructed where it does
   not). Twelve cosine-weighted taps in the hemisphere; each is projected back
   to the screen; the depth there says whether the tap landed on a real
   surface within `GiRadius` and in front of the sample (the same thickness
   reasoning SSR's walk uses), and if it did, that pixel's **lit colour** is
   gathered, weighted by the cosine. The result is an irradiance estimate in
   RGB, at half res.
2. **`ssao_blur` twice**, unchanged, because a noisy gather wants exactly the
   separable blur a noisy occlusion wanted.
3. **`ssgi_apply`**: `out = lit + lit * gi * intensity`.

That last line is the design's one real approximation and it is stated
plainly. A post pass has the lit colour and the normal but **not the albedo**,
and indirect diffuse is albedo x irradiance. Using the pixel's own lit colour
in albedo's place is the standard forward-renderer stand-in and it has the
two properties that matter: a red wall bounces red onto what it lights *and*
receives tinted by its own colour, and a black surface receives nothing --
which is what "albedo x irradiance" says at the ends of the range. What it
gets wrong is a dark surface under a bright light: it reads bright, so it
receives more than it should. The intensity dial is the restraint, exactly as
SSAO's is for its own stated compromise. Off adds no pass and is exact.

Two more stated limits, both inherited from being screen-space: light only
bounces from what is **on screen** (turn away from the red wall and its
bleed goes with it), and only from what is **in front** (a surface behind the
camera plane contributes nothing). These are the failures the traced form
exists to fix.

### The traced form: RT GI in the lit shader

`RenderSettings::RayTracedGlobalIllumination`, offered only while
`RayTracing` is on **and** the device is bindless -- the same gate RT
reflections use, and for the same reason: shading a hit needs the material
heap. It does **not** live in a post pass. It lives in `pbr_fragment.glsl`
under `RV_RAY_GI`, next to `RV_RAY_REFLECTIONS`, because everything it needs
is already bound there and nowhere else: the acceleration structure, the ray
instance table (binding 15), the material heap, the light buffer, and
`TraceReflection` -- which is already "the radiance a ray finds, shaded at
the hit". A reflection ray and an indirect-diffuse ray differ only in which
direction they are cast and how the result is weighted.

Per pixel: **four** cosine-weighted directions about the shading normal,
built from a per-pixel hash of `gl_FragCoord` and the frame counter so the
noise is different every pixel and every frame (TAA is what resolves it, the
same bargain the rest of the renderer already takes); each ray goes through
`TraceReflection`, which returns the sky where it misses and a simplified
shade where it hits. The mean is the irradiance; it multiplies the surface's
own `diffuse` (albedo x (1 - metallic)) and `GiIntensity`, and lands in the
same place the ambient term does. **Albedo is right here** -- the shader has
it -- which is the substantive quality difference from the screen-space form,
alongside seeing off-screen and behind.

Four rays per pixel is a real cost and it is stated: this is the most
expensive switch in the engine, and it is off by default.

### One at a time, and the row that says so

`ResolveRayTracedGlobalIllumination(render)` reads the project's checkbox,
the `--raygi=` override, and what the device can do -- the same three-way
resolve as the other two. When it is true:

- the frame graph **does not add the SSGI passes at all**, whatever the
  profile says (`desc.Post.GlobalIllumination && !rayGi`);
- the lit shaders compile with `RV_RAY_GI`;
- the profile's **Global illumination** row is disabled through
  `FieldHint::DisabledIf(RayGiTakesOver)` and reads "ray-traced equivalent in
  use", which is the note 7ao already writes for SSR and SSAO;
- the **Intensity** dial stays live under either form (`GiDialsApply`), because
  both consult it -- the same rule the AO radius and intensity already follow.

### The checks

`scenetest`: the graph assertions -- SSGI off adds no pass; on adds compute,
two blurs and apply; with RT GI resolved on, the SSGI passes are absent
whatever the profile holds; and the resolve prefers the override to the
project. `check_gi.py` on a new `gi_corner` fixture -- a white floor and a
white wall meeting a **saturated red wall**, one directional light, no sky
light, so the only red in the frame is bounce: the white wall's pixels near
the corner must be redder with GI on than with it off, by a stated margin,
and the far end of the same wall must not be (the bleed falls off). Then the
same scene with `--raytracing=on --raygi=on`: the same corner reddens, and --
the claim the screen-space form cannot pass -- a wall **facing away** from
the red one, whose bounce source is off screen, reddens too. Falsified by
zeroing the gather (no reddening anywhere) and by removing the `!rayGi` gate
(both run and the corner is twice as red).

### What stays stated

One bounce. No indirect specular. No caching, no probes, no radiance cache,
no denoiser beyond the blur and TAA -- so SSGI is soft and RT GI is grainy
until TAA settles. SSGI's albedo stand-in. SSGI sees only what is on screen
and in front. RT GI is four rays a pixel and costs accordingly. Neither
affects the shadow pass, the probes, or the sky.

---

## 7at. Global illumination (9.12): a screen-space pass in the profile, a ray in the shader, and one of them at a time

The owner's ask (2026-08-17): "global illumination which will be a part of
post processing profile, and also ray traced illumination -- its option
should be available in rendering settings under ray tracing when ray tracing
is enabled, and when RT illumination is selected the global illumination gets
disabled/greyed out."

That is exactly the shape 7ao already built twice. **A screen-space effect
lives in the post profile; its traced twin lives in Render Settings under the
ray-tracing switch; the traced one takes over and the profile's row greys
with a note.** SSR/RT reflections and SSAO/RTAO are that pattern; this is the
third pair, and it reuses the machinery rather than inventing a third way:
`ResolveRayTracedGlobalIllumination`, `FieldHint::DisabledIf`, the same
"ray-traced equivalent in use" note, the same `--raygi=` override.

**What indirect light *is* here.** Both forms answer one question -- how much
light arrives at this pixel from the *scene* rather than from a light or the
sky -- and both add it as diffuse. Neither is a full light transport: one
bounce, no specular indirect (SSR and RT reflections are that), no
multi-bounce, no infinite light.

### The screen-space form: SSGI in the profile

`PostSettings::GlobalIllumination`, with `GiIntensity` and `GiRadius`. The
chain is SSAO's chain with a different first pass and a different last one,
which is deliberate: the shape is known to work, the blur is *literally the
same shader*, and the half-resolution decision, the reconstruction of
position and normal, and the projection scales are all already carried in
`PostParams`.

1. **`ssgi_compute`**, half resolution. Position and normal come from the
   depth buffer and the surface attachment exactly as SSAO's do (7ac, 7ae --
   the written normal where it agrees with depth, reconstructed where it does
   not). Twelve cosine-weighted taps in the hemisphere; each is projected back
   to the screen; the depth there says whether the tap landed on a real
   surface within `GiRadius` and in front of the sample (the same thickness
   reasoning SSR's walk uses), and if it did, that pixel's **lit colour** is
   gathered, weighted by the cosine. The result is an irradiance estimate in
   RGB, at half res.
2. **`ssao_blur` twice**, unchanged, because a noisy gather wants exactly the
   separable blur a noisy occlusion wanted.
3. **`ssgi_apply`**: `out = lit + lit * gi * intensity`.

That last line is the design's one real approximation and it is stated
plainly. A post pass has the lit colour and the normal but **not the albedo**,
and indirect diffuse is albedo x irradiance. Using the pixel's own lit colour
in albedo's place is the standard forward-renderer stand-in and it has the
two properties that matter: a red wall bounces red onto what it lights *and*
receives tinted by its own colour, and a black surface receives nothing --
which is what "albedo x irradiance" says at the ends of the range. What it
gets wrong is a dark surface under a bright light: it reads bright, so it
receives more than it should. The intensity dial is the restraint, exactly as
SSAO's is for its own stated compromise. Off adds no pass and is exact.

Two more stated limits, both inherited from being screen-space: light only
bounces from what is **on screen** (turn away from the red wall and its
bleed goes with it), and only from what is **in front** (a surface behind the
camera plane contributes nothing). These are the failures the traced form
exists to fix.

### The traced form: RT GI in the lit shader

`RenderSettings::RayTracedGlobalIllumination`, offered only while
`RayTracing` is on **and** the device is bindless -- the same gate RT
reflections use, and for the same reason: shading a hit needs the material
heap. It does **not** live in a post pass. It lives in `pbr_fragment.glsl`
under `RV_RAY_GI`, next to `RV_RAY_REFLECTIONS`, because everything it needs
is already bound there and nowhere else: the acceleration structure, the ray
instance table (binding 15), the material heap, the light buffer, and
`TraceReflection` -- which is already "the radiance a ray finds, shaded at
the hit". A reflection ray and an indirect-diffuse ray differ only in which
direction they are cast and how the result is weighted.

Per pixel: **four** cosine-weighted directions about the shading normal,
built from a per-pixel hash of `gl_FragCoord` and the frame counter so the
noise is different every pixel and every frame (TAA is what resolves it, the
same bargain the rest of the renderer already takes); each ray goes through
`TraceReflection`, which returns the sky where it misses and a simplified
shade where it hits. The mean is the irradiance; it multiplies the surface's
own `diffuse` (albedo x (1 - metallic)) and `GiIntensity`, and lands in the
same place the ambient term does. **Albedo is right here** -- the shader has
it -- which is the substantive quality difference from the screen-space form,
alongside seeing off-screen and behind.

Four rays per pixel is a real cost and it is stated: this is the most
expensive switch in the engine, and it is off by default.

### One at a time, and the row that says so

`ResolveRayTracedGlobalIllumination(render)` reads the project's checkbox,
the `--raygi=` override, and what the device can do -- the same three-way
resolve as the other two. When it is true:

- the frame graph **does not add the SSGI passes at all**, whatever the
  profile says (`desc.Post.GlobalIllumination && !rayGi`);
- the lit shaders compile with `RV_RAY_GI`;
- the profile's **Global illumination** row is disabled through
  `FieldHint::DisabledIf(RayGiTakesOver)` and reads "ray-traced equivalent in
  use", which is the note 7ao already writes for SSR and SSAO;
- the **Intensity** dial stays live under either form (`GiDialsApply`), because
  both consult it -- the same rule the AO radius and intensity already follow.

### The checks

`scenetest`: the graph assertions -- SSGI off adds no pass; on adds compute,
two blurs and apply; with RT GI resolved on, the SSGI passes are absent
whatever the profile holds; and the resolve prefers the override to the
project. `check_gi.py` on a new `gi_corner` fixture -- a white floor and a
white wall meeting a **saturated red wall**, one directional light, no sky
light, so the only red in the frame is bounce: the white wall's pixels near
the corner must be redder with GI on than with it off, by a stated margin,
and the far end of the same wall must not be (the bleed falls off). Then the
same scene with `--raytracing=on --raygi=on`: the same corner reddens, and --
the claim the screen-space form cannot pass -- a wall **facing away** from
the red one, whose bounce source is off screen, reddens too. Falsified by
zeroing the gather (no reddening anywhere) and by removing the `!rayGi` gate
(both run and the corner is twice as red).

### What stays stated

One bounce. No indirect specular. No caching, no probes, no radiance cache,
no denoiser beyond the blur and TAA -- so SSGI is soft and RT GI is grainy
until TAA settles. SSGI's albedo stand-in. SSGI sees only what is on screen
and in front. RT GI is four rays a pixel and costs accordingly. Neither
affects the shadow pass, the probes, or the sky.

---

## 8. What this changes

| Item | Before | After |
|---|---|---|
| Loop | variable dt everywhere | fixed-step accumulator + interpolation, **before physics** |
| Phase 2 order | play mode first | **loop first**, then play mode |
| Physics | "add Jolt" | batch body adds, 2 broad-phase layers, BodyID not pointers |
| Lights | 8-light cap accepted | clustered forward removes it in Phase 3 |
| Render graph | Phase 3.1 | confirmed, and now has a concrete design |
| Bindless | unexamined | one fork, at the material and its shader block; the RHI stays one interface (§7al) |
| Ray tracing | "no OpenGL path at all" | ray queries only, shadows first, reflections after hit shading exists (§7am); every light kind and skinned casters refit in stage 2 (§7an) |
| ECS | EnTT | confirmed, with the failure mode written down |
| GPU-driven | unstated | explicitly out of scope |
| Contacts | "route them into scripts" | the routing is easy; sleep, removal and sub-shape granularity are not (§3a) |
| Audio | "add miniaudio" | the null backend is the design, not a fallback (§7a) |
| Runtime | "prove nothing leaked into the editor" | it found three defects nothing else could reach (§7b) |
| Verification | zero validation lines | plus exit 0, plus the pixels (§7b) |
