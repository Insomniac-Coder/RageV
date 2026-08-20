# RageV — roadmap

Written 2026-08-08, extended 2026-08-09. Companion to
[HANDOFF.md](HANDOFF.md) (current state) and
[ARCHITECTURE.md](ARCHITECTURE.md) (renderer design).

**Phases 0-5 are MVP+**: the definition of a finished engine for one person.
**Phases 6-8 are past it**, added once phases 0-4 were done and 3 was complete.
The split matters — everything up to 5 was ordered by dependency and each item
blocks something after it. Nothing in 6-8 blocks anything. They are a menu.

**Target positioning, in the user's words:** *ease of use of Unity, somewhat the
graphical fidelity of Unreal, functionality closer to Godot.*

---

## 0. What this document is

A survey of what Unity, Godot and Unreal actually provide; a definition of what
"MVP+" has to mean for a solo-built engine; an honest audit of where RageV
stands against that; and a roadmap ordered by **dependency**, not by appeal.

That ordering claim holds for phases 0-5 and deliberately does not for 6-8.
Past MVP+ there is no spine left to respect: text, particles, packing and
terrain do not depend on each other, and the right next one is whichever a
real game turns out to need.

The single most important finding is in §4. If you read one section, read that
one.

---

## 1. What the three engines actually teach

I went in expecting the gap to be a feature list. It is not. Each of the three
engines is really teaching one lesson, and only one of them is about graphics.

### Unity — "ease of use" is five mechanisms, not polish

Unity's reputation for approachability is not diffuse quality-of-life. It
reduces to five specific mechanisms, and every one of them is buildable by one
person:

1. **Every asset has a stable GUID stored in a `.meta` sidecar.** Move a file,
   rename it, restructure the folders — every reference still resolves. This is
   the mechanism that makes drag-and-drop assignment *trustworthy*, and
   trustworthiness is what makes people use it. Unity caches imported results by
   a hash of source content plus ID, so the mapping is also cheap.
2. **The Inspector is generated from reflection.** Add a field to a component and
   it shows up. No per-component editor code. This is why Unity can have hundreds
   of components without hundreds of editor files.
3. **Prefabs** — an entity subtree saved as an asset, instantiable, with
   per-instance overrides that survive edits to the original.
4. **Play mode is non-destructive.** The scene is snapshotted on Play and
   restored on Stop, so you can edit while running and lose nothing. This is what
   makes the iteration loop feel free.
5. **One obvious place to put behaviour** — a component with a fixed, small,
   well-known callback set.

None of that is rendering. All of it is architecture, and most of it is cheap
*now* and a migration *later*.

### Godot — scope comes from uniformity, and from not writing everything yourself

Godot's feature list is genuinely enormous — three renderers, 2D and 3D physics,
navigation, audio buses with effects, a full GUI toolkit, XR, localisation,
networking, video playback. Reading it as a checklist is demoralising and
misleading. Two things make it achievable:

- **Two concepts cover the entire engine.** Everything is a Node in a tree;
  everything savable is a Resource. Scenes are nodes. Prefabs are scenes. The UI
  is nodes. Uniformity is what lets a small core team ship that breadth — each
  new feature reuses the same editing, serialization and inspection machinery.
- **They stopped writing their own where it wasn't the point.** Godot 4.6 ships
  **Jolt as the default 3D physics engine**, retiring their in-house one. A
  project with hundreds of contributors concluded that maintaining a competitive
  3D physics engine was not a good use of its time. That is an extremely strong
  signal for a solo project.

Also worth noting: Godot's 2D is a genuinely separate first-class pipeline, not
a degenerate 3D case. RageV has the same split already (Renderer2D /
Renderer3D), which is the right shape.

### Unreal — fidelity is a pipeline, and you already own most of it

Unreal's look does not come from better BRDF maths. RageV's Cook-Torrance
implementation is the same family of equations. It comes from:

- HDR throughout, with physically meaningful light units
- A real post chain: exposure, bloom, depth of field, tonemap, temporal AA
- Shadows with serious filtering, at multiple cascades
- Global illumination

Set GI aside — Lumen, Nanite and virtual shadow maps are out of reach and
chasing them would end the project. What is left of the fidelity gap is
**shadows, image-based lighting, an HDR target and a post chain.** Those are
render-target plumbing and well-documented maths. That is the encouraging
finding of this whole survey: the fidelity ambition is the *least* threatening
part of the goal, because the renderer is already the strongest part of RageV.

---

## 2. The bar

**MVP** — someone who is not you can build and ship a small 3D game: import a
few models, light a scene with shadows, add physics and sound, wire input,
script behaviour, press Play, and export an `.exe` they can hand to a friend.
Without writing engine code. Without leaving the editor.

**MVP+** — the above, plus enough that a person would pick RageV over "just use
Godot" for some concrete reason. The `+` is: prefabs, undo/redo, skeletal
animation, IBL and post so scenes look good by default, and C# scripting.

Note what that definition implies. An engine is a **loop**:

```
import asset -> place in scene -> attach behaviour -> press play -> iterate -> export a build
```

RageV can currently do *place in scene* (primitives only) and *attach behaviour*
(barely). **Import, play and export do not exist.** Three of six links in the
loop are severed. That, not any missing rendering feature, is the distance to
MVP.

---

## 3. Capability audit

Scope target is Godot, so that is the comparison column. "MVP+ bar" is what
RageV actually needs — deliberately far below Godot in most rows.

| Capability | Godot | RageV today | MVP+ bar |
|---|---|---|---|
| **Rendering core** | 3 renderers, Vulkan + GL compat | **RHI, 2 complete backends** | ✅ done, ahead of bar |
| PBR materials | Disney PBR, ORM, SSS, clearcoat | **Cook-Torrance, metallic-roughness, 5 maps** | ✅ done |
| Tonemapping | Linear/Reinhard/Filmic/ACES/AgX | **ACES, inside the PBR shader** | move to a post pass |
| Shadows | CSM, cube, PCSS-like blur | ❌ none | CSM directional + cube point + spot |
| IBL / sky | PBR sky, sky shaders, reflection probes | ❌ flat ambient constant | skybox + irradiance + prefiltered + BRDF LUT |
| Post chain | bloom, DOF, SSAO, SSR, TAA, FSR2 | ❌ none | HDR target, tonemap, bloom, FXAA, SMAA |
| GI | SDFGI, VoxelGI, lightmaps | ❌ none | **out of scope** |
| Culling / instancing | clustered, occlusion | ✅ frustum cull + instancing | occlusion, depth sort |
| **Scene graph** | Node tree | flat EnTT registry, **no parenting** | parent/child, world transforms |
| Entity IDs | Node IDs (4.6) | **hardcoded `"12345678890"`** | real UUIDs |
| Prefabs | scenes-as-prefabs, first-class | ❌ none | create, instantiate, override |
| Serialization | text + binary, lossless | YAML, **lossy** (no hierarchy, fake IDs) | lossless round-trip |
| **Asset import** | glTF, .blend, FBX, Collada, OBJ | ❌ **none** | glTF 2.0 |
| Asset database | UID-based, cached | ❌ **none** (path-keyed texture cache only) | UUID + `.meta` + content-hash cache |
| Content browser | full | ❌ none | browse, drag-drop assign |
| **Physics** | Jolt (default in 4.6), 2D + 3D | ❌ **none** | Jolt: rigid/static/character, shapes, triggers, raycast |
| **Audio** | buses, effects, 3D, Doppler | ❌ **none** | 2D + 3D sources, listener, volume groups |
| **Input** | remappable action map | raw keycodes only | named action mapping |
| Animation | skeletal, IK, blend trees, tracks | ❌ none | skinning + clips + blending |
| **Scripting** | GDScript, C#, GDExtension | `ScriptableEntity`, 3 virtuals, **leaks** | rich native C++ API, then C# |
| Play mode | full, non-destructive | ❌ **none** — scripts run in the editor | snapshot / restore |
| **Build / export** | 8+ platforms, PCK packing | ❌ **none** | Windows `.exe` + packed assets |
| Editor camera | full | ❌ **renders through the scene camera** | fly + orbit + focus |
| Inspector | generated, per-type overrides | hand-written per component | reflection-driven |
| Undo/redo | full | ❌ disabled menu entries | command stack |
| UI toolkit | full GUI node set, theming | ❌ none | screen-space sprites + text |
| Navigation | A*, navmesh, avoidance | ❌ none | **out of scope** |
| Networking | ENet, WebRTC, WebSocket | ❌ none | **out of scope** |
| XR | OpenXR | ❌ none | **out of scope** |

**Read the shape of that table, not the rows.** Everything RageV has is in one
band — the renderer. Everything it lacks is everything else. This is the
inverse of the usual hobby-engine failure, where someone builds a scene editor
and asset browser around a renderer that draws untextured cubes. The renderer
being done first is genuinely valuable; it also means none of the remaining work
gets to reuse the muscle already built.

---

## 4. The dependency spine

This is the part that determines the order, and it is not intuitive.

```
                      ┌──────────────────┐
                      │  Entity UUIDs    │◄──── currently hardcoded
                      └────────┬─────────┘
                               │
        ┌──────────────────────┼───────────────────────┐
        ▼                      ▼                       ▼
┌───────────────┐     ┌────────────────┐     ┌──────────────────┐
│  Transform    │     │  Lossless      │     │  Asset handles   │
│  hierarchy    │────►│  serialization │◄────│  (+ .meta files) │
└───────┬───────┘     └───────┬────────┘     └────────┬─────────┘
        │                     │                       │
        │                     ▼                       ▼
        │             ┌───────────────┐      ┌──────────────────┐
        │             │  Play mode    │      │  glTF import     │
        │             │  (snapshot)   │      │  (node trees!)   │
        │             └───────┬───────┘      └────────┬─────────┘
        │                     │                       │
        └──────────┬──────────┴───────────────────────┘
                   ▼
            ┌─────────────┐        ┌──────────────┐
            │  Prefabs    │        │  Packaging   │
            └─────────────┘        └──────────────┘
```

Four consequences follow, and each one reverses an intuition:

**1. UUIDs are the deepest blocker and the cheapest fix.** Prefabs, entity
references in scripts, parent links in a scene file, and play-mode state
restoration all need to name an entity durably. Today `SceneSerializer` writes
the literal string `"12345678890"` for every entity. Adding real IDs is perhaps
an afternoon; adding them *after* scenes and prefabs exist in the wild is a
format migration.

**2. glTF import needs the transform hierarchy, not the other way round.** A
glTF file is a node tree. Without parenting, importing anything more structured
than a single mesh loses information silently. Hierarchy must land before the
importer, or the importer gets written twice.

**3. Play mode is a serialization feature.** Non-destructive play is
*snapshot the scene → run → restore*. It is only as correct as serialization is
lossless. Right now serialization drops hierarchy (none exists), identity (fake
IDs), and script bindings — so play mode built today would silently destroy
people's scenes. Serialization completeness is the prerequisite, and a
save→load→save byte-identical test is the way to know you have it.

**4. Reflection is a force multiplier, so it belongs early.** Every phase below
adds components. Without a reflection layer, each one costs hand-written
inspector UI *and* hand-written serializer cases *and*, later, hand-written C#
marshalling — three places to forget. With one component descriptor, all three
are generated. **EnTT already ships `entt::meta`, and EnTT is already
vendored**, so this costs no new dependency.

The same argument applies to **undo/redo**: it is a discipline, not a feature.
Every mutation added after it exists routes through the command stack for free.
Every mutation added before it has to be found and retrofitted.

**The one structural freedom:** fidelity work (§Phase 3) depends on almost none
of this. It is a parallel track against the RHI, which is already stable. If
motivation ever demands a visible win, that is the branch to pull forward
without incurring debt.

---

## 5. Roadmap

Sizes are relative: **S** ≈ a sitting, **M** ≈ a few, **L** ≈ a focused week,
**XL** ≈ multi-week.

### Phase 0 — Foundations *(do not skip; everything downstream references it)*

| # | Item | Size | Why here |
|---|---|---|---|
| 0.1 | **Editor camera** (fly + orbit + focus-on-selection) | M | You cannot evaluate any later work through a scene camera. Highest value per hour in the whole document. |
| 0.2 | **Entity UUIDs** + `IDComponent` | S | Deepest blocker, cheapest now |
| 0.3 | **Transform hierarchy** — parent/children, local vs world, cached world matrices | M | Blocks prefabs, glTF import, skinning, attachments |
| 0.4 | **Component reflection** via `entt::meta` → generated inspector + generated serializer | L | Force multiplier; pays back every later phase |
| 0.5 | **Undo/redo** command stack | M | Discipline. Retrofitting means auditing every mutation site |
| 0.6 | **Round-trip test** — save → load → save is byte-identical | S | The only way to know serialization is lossless |
| 0.7 | **Fix native script lifetime** (see §8) | S | Live use-after-free and leak |

### Phase 1 — Content pipeline *(the missing left half of the loop)*

| # | Item | Size |
|---|---|---|
| 1.1 | `AssetHandle` (UUID) + asset registry + `.meta` sidecars | L |
| 1.2 | Import cache keyed by content hash | M |
| 1.3 | **glTF 2.0 import** (fastgltf) — meshes, materials, textures, node trees | L |
| 1.4 | Mesh / Material / Texture become real assets; `MeshComponent` holds a handle, not a `PrimitiveType` enum | M |
| 1.5 | Content browser panel + drag-drop onto inspector fields | L |
| 1.6 | **Prefabs** — create, instantiate, per-instance overrides | L |

### Phase 2 — Simulation *(the missing middle of the loop)*

Reordered after the research pass — see [ENGINE-NOTES.md](ENGINE-NOTES.md) §1.
The fixed-timestep loop moved to the front because it is a **prerequisite** for
physics, not a companion to it: integrator behaviour depends on `dt`, so a
variable one means a scene that settles at 300 fps explodes at 40. Landing it
after Jolt would mean writing the transform sync twice.

| # | Item | Size |
|---|---|---|
| 2.0 | **Fixed-timestep loop** — accumulator, frame-time clamp, render interpolation | M |
| 2.1 | **Edit/play split** — `OnUpdateEditor` / `OnUpdateRuntime`, snapshot on Play, restore on Stop | M |
| 2.2 | Input action mapping (named actions over raw keycodes) | S |
| 2.3 | **Native script API v2** — entity handle, components on any entity, input, time, instantiate/find/destroy, scene queries | L |
| 2.4 | **Physics — Jolt** — rigid/static/character bodies, box/sphere/capsule/convex/mesh shapes, triggers, raycasts, debug draw. Batch body adds, two broad-phase layers, `BodyID` not pointers (ENGINE-NOTES §3) | XL |
| 2.5 | **Audio — miniaudio** — 2D + 3D sources, listener, volume groups | L |
| 2.6 | Collision and trigger callbacks routed into scripts | M |

**Phase 2 is complete.** Two things came out differently from how they were
written here:

- 2.4 shipped without **debug draw** and without **convex/mesh colliders**.
  Debug draw is a renderer feature that happened to be listed under physics;
  it is now the largest gap between "physics works" and "physics is usable",
  and the demo scene contains a trigger volume nobody can see.
- 2.6 turned out to be the harder half of the pair, not the easier one. The
  callbacks themselves are a morning's work; the two Jolt behaviours around
  them — contacts withdrawn on sleep, and removal reported a step late — are
  the part that decides whether a script can trust what it is told. Both are
  recorded in HANDOFF §5.

### Phase 3 — Fidelity *(parallel track; the "somewhat Unreal" slice)*

| # | Item | Size |
|---|---|---|
| 3.1 | **Render graph** — declare passes and resources, derive barriers | L | ✅ built smaller: the RHI already tracks layouts, so it owns targets, describes the frame and validates it. See ENGINE-NOTES §6. |
| 3.2 | HDR target + tonemap/bloom post chain (moves ACES out of the PBR shader) | M | ✅ plus FXAA, and SMAA in 7.9 |
| 3.3 | Skybox + cubemaps (`TextureType::TextureCube` already exists in the RHI) | M | ✅ three background modes, CPU panorama-to-cube conversion, plus reflection probes (baked and realtime), which were not on this list |
| 3.4 | **IBL** — irradiance, prefiltered specular, BRDF LUT; replaces the flat ambient term | L | ✅ all three: CPU hemisphere convolution for irradiance, GPU importance-sampled GGX per roughness level, and a CPU-integrated BRDF table checked against its analytic corners |
| 3.5 | **Shadows** — CSM directional, cube point, spot. Sphere-fit cascades, texel snapping, normal-offset bias (ENGINE-NOTES §5) | XL | ✅ all three types; cascade fitting checked in scenetest rather than by eye; four spot and four point lights cast at once |
| 3.6 | Frustum culling, draw sorting, instancing. **CPU only** — GPU-driven rendering is out of scope | M | ✅ done. Per-pass frustum culling, then grouping by mesh and bound material state into instanced draws. A 1000-mesh scene: 18018 considered, 14780 culled, 3238 drawn, **60 after batching**. OpenGL 7.17 ms → 1.59 ms; Vulkan 1.96 → 1.88 ms, which was never submission-bound. Opaque draws are grouped, not depth-sorted — a front-to-back sort needs a measurement first |
| 3.8 | **Clustered forward lighting** — removes the 8-light cap, keeps transparency working (unlike deferred) | L | ✅ done. 16x9x24 cells, lights binned on the CPU, directional lights kept unbinned. Image identical to the unclustered loop at 3, 48 and 256 lights. 256 local lights: Vulkan 5.56 → 3.15 ms, OpenGL 7.03 → 3.48 ms. **A loss where every light reaches the whole scene** — the busiest cell then holds all of them and the indirection buys nothing; the benchmark reports that number |
| 3.7 | Skeletal animation — skinning, clips, blending | XL | ✅ done. Skeleton, clips, sampling and blending; glTF skin/animation import against a generated asset; SkinnedVertex with its own pipeline; skinned PBR *and depth* shaders sharing the static ones' lighting through includes; AnimatorComponent. `scenes/skinning.rage` bends, and its shadow bends with it |

**On 3.1:** a render graph is not architecture astronomy here. It earns its
keep at roughly three passes, and Phase 3 adds five or six. Sync bugs have
already cost real time on this project (§HANDOFF, three separate barrier
defects). Hand-writing barriers six more times is the expensive option.

### Phase 4 — Ship *(the definition of done for MVP)*

| # | Item | Size |
|---|---|---|
| 4.1 | Project concept — a folder is a project, with settings and a start scene | M |
| 4.2 | Standalone runtime target (engine without the editor) | M |
| 4.3 | Packaging — cook assets, pack, emit `.exe` + data | L |

**Phase 4 is complete.** 4.3 shipped as a folder rather than an archive, and
without cooking, both deliberately: packing needs a virtual file system on the
loading side to be worth anything, because every asset path in the engine goes
through `std::filesystem`. A folder that runs is the whole of the value and
none of that cost. `rvpack` is a headless tool as well as an editor menu item,
since packaging touches no GPU and a build step that can be scripted is one
that gets run.

4.3 also turned up that **no Release or Dist build had ever been produced** in
this project, which was the real first task. All three configurations build and
pass now. Dist logs at warn rather than not at all: a player who can send you
one line about what went wrong is worth more than a silent binary.

4.2 is also a **forcing function**: it proves nothing load-bearing has leaked
into the editor.

**4.1 and 4.2 are done.** The forcing-function argument held, and then some --
4.2 found three defects in its first hour that no test and no amount of editor
use could have surfaced, because all three needed something that draws a scene
to the swapchain and then exits:

- The Vulkan swapchain barrier assumed one pass per frame, and named
  `UNDEFINED` as the old layout every time. A second pass could legally come
  back to a discarded image.
- The UI pass cleared the backbuffer unconditionally, which is free in the
  editor and erases the frame in a game. The runtime rendered correctly and
  showed a blank window.
- `Layer`'s destructor was not virtual, so no layer's derived members had ever
  been destroyed -- every scene, render target and material leaked on every
  shutdown, for the life of the project.

The last one is the argument in miniature. It was not a runtime bug; it was a
bug the runtime was the first thing to *expose*, because nothing before it had
ever exited cleanly enough to notice. **Verify by exiting, not by killing** is
now part of the bar.

Also landed alongside: collider debug draw, click-to-select in the viewport,
and `RHIDevice::RequestCapture` -- a backbuffer capture, because draw-call
counts cannot tell a rendered frame from one that was cleared afterwards.

### Phase 5 — C# scripting *(the second option the user asked for)*

| # | Item | Size |
|---|---|---|
| 5.0 | **Wrap every third-party type out of the public API** | L |
| 5.1 | CoreCLR host via `nethost`/`hostfxr` | L |
| 5.2 | Interop layer — `[UnmanagedCallersOnly]` + function-pointer tables | L |
| 5.3 | Managed class library mirroring the native API | L |
| 5.4 | Project assembly build (editor invokes `dotnet build`) | M |
| 5.5 | Hot reload via collectible `AssemblyLoadContext` | L |
| 5.6 | Script fields exposed in the inspector | M |

**Why last:** C# must mirror a *stable* native surface. Every change to the
native API after C# exists costs a binding update, a marshalling update and a
class-library update. Design once, bind twice — in that order. Building C# early
is, in my judgement, the single most likely way to stall this project.

**5.0 is new, and it is first for that same reason.** No third-party type may
appear in a public RageV header: `glm::vec3` becomes an engine type, and every
other library the engine uses but did not write — EnTT, Jolt, miniaudio, spdlog,
yaml-cpp — is checked for the same leakage. The engine should read as one engine
rather than as an assembly of libraries, and nobody writing a script should have
to learn glm's name to use it.

The same change segregates the public API into domain namespaces —
`RageV::Math::Vec3`, `RageV::Audio::`, `RageV::Physics::`, `RageV::Assets::`,
with `RageV::RHI::` already in that shape. Both halves are one API break, so they
land together rather than breaking every include twice.

Two naming rules, settled rather than assumed. **Types are PascalCase** —
`Vec3`, `Mat4`, `Quat` — matching `Entity`, `Timestep` and `AssetHandle` rather
than glm's lowercase, so that a reader can see the type is the engine's and not
glm's under a different include. And **there is no `RV` namespace or alias**;
`RageV` is the only spelling.

One thing still to watch: `Renderer`, `Scene` and `Input` already exist as
*types* at the scope where those namespaces would go. Each needs a different
namespace name or the type moved, and finding that out during the rename is the
expensive way.

It has to be a real wrapper rather than a typedef. An alias still puts
`glm::vec3` in every compiler error, in every IDE tooltip and in the generated
manual, so it hides nothing. Keeping the wrappers layout-compatible makes the
conversion at the `.cpp` boundary free, and demotes the third-party header to an
implementation include.

The ordering is not a preference. 5.1 carries no engine types and is unaffected,
but binding 5.2 and 5.3 against `glm::vec3` and *then* renaming it means doing
the interop, the marshalling and the class library twice.

### Phase 6 — Text, UI and particles *(what a game needs that an engine demo does not)*

Everything up to here draws a *scene*. A game also draws a score, a menu, a
health bar, a puff of smoke — and none of that is expressible today. This is the
first phase since Phase 1 whose absence a player would notice rather than a
developer.

| # | Item | Size |
|---|---|---|
| 6.1 | Font atlas and glyph cache — MSDF, so text stays sharp at any size | L |
| 6.2 | Text rendering — layout, wrapping, alignment, colour | L |
| 6.3 | Screen-space canvas — anchoring, scaling, a z order that is not the draw order | M |
| 6.4 | UI widgets — sprite, label, button, slider, with input hit-testing | L |
| 6.5 | 2D particles — emitters, curves over lifetime, additive and alpha blending | L |
| 6.6 | 3D particles — the same simulation, billboarded and depth-sorted | M |
| 6.7 | GPU particle simulation, if 6.5 measures badly and only then | M |

**MSDF rather than a bitmap atlas.** A bitmap font is one texture and an
afternoon, and it is soft at every size but the one it was baked at. Multi-channel
signed distance fields cost a generation step and stay crisp from 8px to 200px,
which is the difference between a UI that scales with the DPI work already done
and one that does not.

**The canvas is not ImGui.** ImGui is the *editor's* UI and is immediate-mode,
which is right for tools and wrong for a game: a game's UI is authored in a
scene, serialized, and driven by scripts. These are entities with components
like everything else.

**Particles are CPU-simulated first, deliberately.** A CPU emitter is a few
hundred lines against the existing quad batcher, and the engine draws a
thousand-mesh scene in 1.9 ms — there is a great deal of headroom to spend
before a compute pass is the answer. 6.7 exists so the question gets asked with
a measurement rather than assumed, which is the rule this renderer has been
wrong about twice.

**Depth sorting arrives here whether or not 3.6 wanted it.** Alpha-blended
particles must be drawn back to front; opaque geometry merely benefits. The
sorting infrastructure is shared.

---

### Phase 7 — Finishing the pipeline *(the deferred debts, collected)*

Nothing here blocks a game being made. Everything here is something a person
shipping one would eventually ask for, and each was deferred with a reason
rather than missed.

| # | Item | Size |
|---|---|---|
| 7.1 | Archive format (`.pak`) **and** the virtual file system it needs | L | ✅ done 2026-08-13. A mount shadows a directory: the VFS answers the same paths the filesystem would, so no call site can tell where bytes come from. Packaging writes `content.pak` (`--loose` keeps the folder form); `Project::Load` mounts it. Proven by pixel diff: Knockdown packaged both ways renders identically to the run-to-run noise floor on both backends — and the diff caught the one existence check that had not gone through the VFS, which cost the packaged HUD its font. Design: ENGINE-NOTES §7h |
| 7.2 | Asset cooking — parse glTF and decode images at build time, not at load | L | ✅ done 2026-08-13. Cooking is a content transform inside the pak build: `.rvtex` (pre-decoded, pre-mipped, BCn via vendored stb_dxt) and `.rvmesh` (the import, serialized) replace source bytes under unchanged paths, and loaders sniff the magic. Measured on the 4K set: the closeup run drops 3.7s→2.0s, VRAM arithmetic ~0.9GB→~0.15GB, visual delta mean 0.89/255 — under the existing cross-backend residual. The cooked mesh path is pixel-exact. `rvpack --raw` ships source bytes. Design and numbers: ENGINE-NOTES §7i |
| 7.3 | Materials as assets, so two entities can share one from the inspector | M | ✅ done 2026-08-13. `.rmat` carrying the five texture maps by handle, plus per-entity scalar overrides — the asset is what the descriptor set holds, the override is what the instance stream holds, which is the line `Material::GetBatchKey` already drew. **The real defect was not sharing but persistence**: `Material` has had all five maps since phase 3 and only the glTF importer could set them, so an imported model lost its textures on the first save. Legacy inline materials convert to overrides losslessly — demo.rage renders pixel-identical |
| 7.4 | Billboard icons for lights, cameras and probes, and picking that hits them | M | ✅ done 2026-08-13. Marks for lights, cameras, probes and audio sources, billboarded through the UI renderer's existing world layer — no new pipeline, and editor-only structurally like the grid. The drawer and the picker share one `Collect` and one `Radius`, so the hit area cannot drift from what is drawn; constant *angular* size is what lets the picker size the mark from the ray's origin alone. Also fixed the toolbar's grid mark, which was reported twice as uneven and turned out to be two ImGui rasterizers resolving half a pixel apart — 9.4/255 mirror asymmetry down to 0.7. ENGINE-NOTES §7j |
| 7.5 | Animation blend component — `BlendPoses` is written and tested, nothing drives it | M | ✅ done 2026-08-13. A change of `Clip` starts a cross-fade; no Play call, so the inspector, a script and a loaded scene all get the same easing. The outgoing clip keeps running while it fades, because a frozen source slides mid-stride. Checked against Khronos' fox — 24 bones, three real clips — through the real `UpdateAnimators`, with a zero-blend control proving the easing is the blend and not the clips differing. ENGINE-NOTES §7k |
| 7.6 | Skinned bounds that cover the animation, not just the bind pose | M | ✅ done 2026-08-13. A box per bone in bone space from one vertex pass, then the union over sampled poses of every clip — conservative, and cheap enough to do at load. All eight corners transformed, since a rotated box's extent is not recoverable from two. Verified on the fox: its animated bounds strictly contain the bind pose and are larger, which is precisely what was culling it early. ENGINE-NOTES §7k |
| 7.7 | Per-object reflection probe selection, replacing the per-scene choice | M | ✅ done 2026-08-12. One cube array indexed per instance, sky at slot 0; the choice rides in the instance so a run never splits. Not one array per resolution -- that reintroduces a per-draw decision. Also gave probes diffuse light, which they had never had, and found three latent RHI bugs including a Vulkan device feature never enabled |
| 7.8 | Front-to-back depth sorting for opaque draws | S | ✅ done 2026-08-13. Justified by measurement first, as this row demanded — and the measurement moved the design. **Instances are ordered *within* each batch**, not globally: a global depth sort orders perfectly and dissolves the grouping instancing needs, measuring 0.567 ms against 0.548 for the existing order on 1500 spread-out meshes. Sorting runs alone is not enough either — 200 slabs sharing one mesh are a *single* batch, so it did nothing there. Doing both: **0.32 ms against 33.9 ms** on that scene, a factor of ~100, and 0.546 vs 0.607 on the stress scene with far less variance. Pixel-identical and draw-count-identical on both backends; `--depth-sort=off` and `check_depth_sort.py` keep it measurable. ENGINE-NOTES §7m |
| 7.9 | SMAA | M | ✅ done 2026-08-14, diagonals in 7.14. Three passes — edges, coverage weights, blend — and **no vendored lookup tables**, which this row said it would need: SMAA's AreaTex answers "what fraction of this pixel did the line cover", and that is a trapezoid, about fifteen lines of arithmetic that a modern GPU runs for less than the dependent texture fetch it replaces. Measured on a scene that is one straight edge at a known angle, so the exact coverage is computable: **coverage error 0.1291 → 0.0210 at 8°**, six times better than no filter and five times better than FXAA, both backends agreeing to four decimals and zero pixels touched away from the edge. Costs 0.072 ms against FXAA's 0.019 at 1600×900. **The check found FXAA had two inverted signs and had been a complete no-op on any clean edge since it was written.** The near-diagonal figures first quoted here were measured at exactly 45°, which is degenerate, and are corrected in 7.14. ENGINE-NOTES §7n |
| 7.10 | TAA -- needs motion vectors first, which also buy motion blur and upscaling | L | DONE 2026-08-14, designed in ENGINE-NOTES 7r. All three prerequisites: motion vectors (shared with 9.5), a Halton jitter indexed by frame number, and a caller-owned history with YCoCg neighbourhood clipping. On a static scene it is the best of all six modes at three of four angles on the linear-light yardstick -- 0.0444 at 47 degrees against SSAA 0.0610 and MSAA 0.0609. **The moving check 7r demanded before calling this finished now exists** (check_taa_motion.py): a textured patch falling 16 px a frame against a 4x supersampled render of the same instant, which settled the reprojection sign by measurement (19.8 as shipped, 29.9 inverted) and found the real defect -- a feedback default of 0.9 chosen when every scene here was static, worse than no filter in motion. Now 0.6: within 2% of no filter moving, 3.1x better still. **The recurring trap, three times over: the neighbourhood clip hides reprojection errors wherever the neighbourhood is uniform.** A flat block showed no ghosting with the sign inverted; a smooth sky is byte-identical whether its velocity is right, zero, or a wild constant. Only per-pixel detail can show one. Catmull-Rom history sampling was implemented, measured at no difference, and reverted; the sky velocity was implemented, measured at no difference, and kept -- it is one matrix multiply and 9.5 has no clip to save it |
| 7.12 | SSAA — render larger, box-filter down | S | ✅ done 2026-08-14. A scale on the scene target and a box-filter resolve, **before bloom and before tone mapping** — averaging is only meaningful where the numbers add up, and bloom thresholding the supersampled image would let one bright subsample light a whole output pixel. Still the only mode that anti-aliases *shading* rather than geometry. Costs almost exactly the square of the factor: render graph GPU 0.591 ms unfiltered, 1.706 at 2×, 6.881 at 4×. The interesting measurement was not the cost but the **yardstick** — SSAA scored badly until the check learned that mixing in linear light and mixing after the tone curve are two different definitions of correct, and each mode can only reach one of them. ENGINE-NOTES §7o |
| 7.14 | SMAA's diagonal pass | M | ✅ done 2026-08-14. Justified by measurement rather than by the paper: at 43° SMAA managed 1.6× where a shallow edge got 6.1×, because a near-diagonal run is one pixel long and the orthogonal reconstruction has nothing left to work with. **43° now 0.0849 → 0.0403, and 8° does not move at all**, which was the failure mode to watch for. No second lookup table — the coverage is one more clamped-line integral, checked against numeric integration before it reached a shader. Two things the design got wrong and only rasterizing found: the "both flags on one pixel" signature exists *only* at exactly 45°, so there is one search rather than two; and "longest run wins" measurably **hurt** small features until it had to win by a margin of 8. Costs 0.040 ms. ENGINE-NOTES §7p |
| 7.13 | MSAA | L | ✅ done 2026-08-14. Multisampled targets on both backends with the resolve where each API already puts it, and `GetColorTexture` handing out the resolve so the frame graph never learns MSAA exists — its only change is a number on one target description. **The best mode on every measurement and nearly the cheapest**: coverage error 0.1730 → 0.0434 at 8°, 0.2691 → 0.0817 at 77°, and 1.932 on the fan against SSAA 2×'s 2.268 — for **+0.053 ms** where SSAA at the same four samples costs +1.153, which is also the proof the fragment shader runs once per pixel. Four real defects were fixed on the way and none was the crash; the crash was `if` where the pool grow needed `while`, in code MSAA was simply the first thing to stress. ENGINE-NOTES §7q |
| 7.11 | Startup: an import cache, and loading off the main thread | M | ✅ done 2026-08-13. Reported as "Not Responding on launch"; the measurement said `Project::Load` was 0.18s of 4.79s and 3.2s was decoding 198 MB of PNG on the first *draw*. The 7.2 cookers already existed and had one caller — the packager — so the editor re-decoded everything every launch. They now run on import into a git-ignored `<project>/Cache`, keyed on the `.meta` hash with the hash in the filename, so a lookup is a stat. Loading moved to a worker behind a progress screen shared with the runtime; uploads step on the main thread so the bar covers them too. Editor 4.79s → 2.31s warm; first open 17.2s → 4.9s on four workers (capped by memory: one 4K cook holds a 256 MB float mip 0). Found and fixed a live defect on the way: the packager cooked font atlases, block-compressing a distance field. ENGINE-NOTES §7l |

**7.1 and 7.2 are one item wearing two hats.** Packing without a virtual file
system is worthless, because every asset path in the engine goes through
`std::filesystem`; and cooking without packing saves load time but not the
awkwardness of shipping a folder. Do both or neither.

**7.5 and 7.6 are debts skeletal animation created.** Clips snap rather than
ease because nothing calls the blend that exists, and a limb swinging wide
leaves a bounding box computed from the bind pose — so a character can be culled
while part of it is still on screen. Both are recorded in HANDOFF §9.

**7.8 should be justified by a measurement.** Draws are already grouped by mesh
and material, which is the half of 3.6 that was worth doing; sorting by depth as
well helps early-z and nothing else. Two "obviously worth it" optimisations in
this renderer have measured as worth nothing.

---

### Phase 9 — Post processing *(the pass that already exists, extended)*

The chain today is **bloom, ACES tonemap and a choice of six anti-aliasing
modes**, in `PostProcess`, running over the linear HDR target before it
reaches the swapchain. Everything below hangs off that same pass, which is
why this is a phase of small items rather than a rewrite: the target, the
resolve and the tonemap ordering already exist.

**Where a setting goes is settled (9.0, done).** Every item here adds fields
to `PostSettings` and to `PostSettingsRegistry`, and nothing else needs
touching: that pair is what puts a setting on disk in a `.rvpostprofile`, in
the inspector under the camera that names it, and in front of a C# script.
ENGINE-NOTES 7s.

**Read this before picking one:** every effect here is cheap to add and easy
to overuse. The demo scene already looked "dead" once for reasons that turned
out to be exposure and a featureless sky (§7g), not a missing effect — so an
effect that hides a lighting problem is worse than no effect.

| # | Item | Size | Notes |
|---|---|---|---|
| 9.0 | **Render settings on the project, a post profile on the camera** | M | **Done 2026-08-14.** The home every item below stores its settings in |
| 9.1 | Colour grading via a 3D LUT | S | **Done 2026-08-14.** `.cube` loading, `Texture3D` in both backends, applied after the tone curve so a LUT from any grading tool does here what it did there. The check that decides it: an identity LUT is byte-identical to no LUT, which is the only way to catch a half-texel sampling error — see ENGINE-NOTES 7t |
| 9.2 | Auto exposure / eye adaptation | M | DONE 2026-08-15, designed in ENGINE-NOTES 7y. A 256-bin log2 luminance histogram via compute, reduced over a percentile window -- a log average is dragged by the sun and one specular hit, which is the "pumping" people complain about. **The pin the roadmap asked for already existed**: `--frame-time` makes everything downstream a function of the frame number, so the rule is simply that the adaptation reads the frame time the loop hands down and never a clock. A new `--exposure=fixed` flag would have been a second thing every check had to remember. And the real protection is that it is **off by default and off exactly**, so check_smaa, check_color_grading, check_taa_* and check_lens_effects all kept their recorded thresholds unchanged. State is **per frame chain** -- 7u's ghost one layer up, since two chains sharing one adapted value would each drag the other's exposure. Manual Exposure becomes exposure compensation. Prerequisite landed on the way: the render graph learned **compute passes**, which 9.5, 9.6 and 9.7 all want |
| 9.3 | Vignette, chromatic aberration, film grain | S | DONE 2026-08-15, designed in ENGINE-NOTES 7w. Three effects at three points in the one pass, because each models something different: aberration is three radial taps of the linear sample, the vignette is still linear because it is *less light arriving* rather than a shadow painted on the result, and grain lands after the curve **and** after the LUT -- run it before and the grade re-maps the noise, so grain would change character with the look. All three off by default and **off is exact**: the shader branches past each rather than computing a no-op, so every recorded threshold in the repository stays valid. **The one that bites is grain**: animated noise makes the frame a function of when it was drawn, so it is seeded from the frame number exactly as TAA's jitter is (7r). check_lens_effects.py asserts all four properties and each was confirmed able to fail -- inverting the vignette fails the direction test, and seeding grain from a clock fails the reproducibility one by 45/255. **Revised the same day** (ENGINE-NOTES 7x): the grain looked wrong despite passing everything, and was three defects rather than one -- a grid of squares instead of clumps, a response curve that was loudest at *black* under a comment claiming the opposite, and one monochrome value for three channels. Now two octaves of value noise (not gradient noise: Perlin is zero at every lattice point, which is a grid of grain-free dots), `sqrt(4L(1-L))`, and the finest octave per channel. Two more assertions, and a tone-ramp scene to make the response measurable at all -- the AA scene every other check builds on is two flat levels, which cannot show that an effect varies with brightness |
| 9.4 | Depth of field | M | DONE 2026-08-15, designed in ENGINE-NOTES 7z. A thin-lens circle of confusion -- focus distance, focal length, f-number -- then a half-resolution golden-angle gather and a full-resolution composite. Two of the three claims in the old entry needed revising: the depth buffer was a graph resource **nobody could read** (SampleDepth was never set, now on unconditionally for the reason the velocity attachment gives), and it is not separable, because a disc is not. Runs after the AA resolve, not before -- reprojecting a temporal filter over an already-defocused image asks its neighbourhood clamp to reconcile a blur with a history blurred differently. **The two bugs both came from the same place**: a gather sized by the destination pixel can never see a defocused foreground arrive, and a per-tap reach test alone lets a blurred background bleed over a sharp foreground it is actually occluded by. Fixed resolution pattern, so unlike 9.2 and 9.3 this adds no determinism hazard |
| 9.5 | Motion blur | M | DONE 2026-08-15, designed in ENGINE-NOTES 7ab. A tile-max / neighbour-max gather along the velocity buffer 7.10 already built -- no velocity is invented, which is the whole reason this waited for TAA. **Unblocked 2026-08-14: 7.10 added the motion vectors.** The scene target carries an RG16F velocity attachment, every instance carries its previous world transform, and the sky reports camera-rotation motion — that last one measures nothing under TAA, which has a neighbourhood clip, and exists precisely for this item, which does not. Read ENGINE-NOTES 7r before using the buffer: velocities are jitter-free by construction, and skinned meshes report the object's motion rather than the limb's, because bones are not double-buffered |
| 9.6 | SSAO / GTAO | M | DONE 2026-08-15, designed in ENGINE-NOTES 7ac. A half-resolution hemisphere gather over depth, blurred separably and applied as a multiply on the ambient term rather than on the whole image -- occlusion darkens indirect light, not direct. The question the roadmap asked (what does this add over the occlusion maps already sampled?) is answered by the fixture rather than asserted: a corner seam that no map contains |
| 9.7 | Screen-space reflections | L | DONE 2026-08-15, designed in ENGINE-NOTES 7ad. **The first new attachment since velocity**: the scene target grew an RGBA8 normal + roughness plane, because a march needs the shaded surface's normal and reconstruction from depth cannot give a roughness. A ray marched in view space, then in screen space (9.10). The "fallback to the probe, not a replacement" instinct in the old entry was right and 9.9 made it exact |
| 9.8 | SSAO reads the real normal | S | DONE 2026-08-15, ENGINE-NOTES 7ae. The reconstruction frame stated once, in one place, instead of three effects each deriving it -- which is what exposed the transform bug. **9.8b, the same day**: the written normal is used only where it agrees with the geometric one within 16 degrees, because a normal map belongs to the surface and an occlusion query belongs to the geometry |
| 9.9 | SSR replaces the probe exactly | M | DONE 2026-08-15, ENGINE-NOTES 7af. The blend moved out of the post pass and into the lighting, one frame late: where the march finds a hit, the probe's contribution is *removed* rather than added to, so a mirror shows the room instead of the room plus a cubemap of it. Measured against the law rather than eyeballed |
| 9.10 | Hi-Z for the SSR march | M | DONE 2026-08-15, ENGINE-NOTES 7ag. The march became a screen-space DDA with a real crossing test and a depth pyramid over it. **The pyramid helps less than the literature promises here**, and the entry says by how much rather than claiming the win |
| 9.11 | The probe convolution's serialized barriers | S | DONE 2026-08-15, ENGINE-NOTES 7ah. Six passes and six copies instead of thirty-six, which is a Vulkan-only stall V.3 found by measuring where the frame's waiting time actually went |
| 9.12 | Global illumination | L | **DONE 2026-08-17, ENGINE-NOTES 7at.** The owner's ask, and the third instance of the pattern 7ao established: **a screen-space effect in the post profile, its traced twin in Render Settings under the ray-tracing switch, and the profile's row greyed with a note while the traced one runs.** SSGI is four passes (half-res gather of the lit colour, its own blur twice, then `lit + lit*gi*intensity`); RT GI is four cosine rays a pixel through the same `TraceReflection` the mirror ray uses, added to `irradiance` so the albedo is the surface's own. Measured on a white corner beside a red wall: SSGI **+0.77** levels of red near the corner and **+0.00** far along the same wall; RT GI **+2.72** near, and **+1.26** on a wall whose bounce source is *off screen*, where SSGI manages **+0.00** -- the difference the two forms exist to have. An intensity of zero is the frame without it, to the byte, on both backends. Stated limits: one bounce, no indirect specular, no denoiser beyond the blur and TAA, and SSGI stands the lit colour in for an albedo it does not have. **Three bugs on the way, all written up**, including the one worth remembering: `SceneUniforms` is mirrored by hand in two GLSL files, and a field added to one of them makes OpenGL -- which links the stages into one program -- render differently run to run |
| 9.13 | GI restructured: one indirect buffer, denoised | L | **DONE 2026-08-18, ENGINE-NOTES 7av-7az.** The owner's four follow-ups against 9.12's stated limits -- a second bounce, and SSGI's albedo, sharpness and off-screen fixes -- which the question "what about denoising?" turned from four patches into one restructure. **RT GI ran inside the lit shader, so its four-ray noise was inseparable from direct light and there was nowhere to put a denoiser.** So both forms now write one albedo-free `Indirect` buffer, and the lit shader reads it **one frame late** and multiplies by the surface's own albedo -- 9.9's pattern for SSR radiance. That multiply moving back into the shader **retired the albedo ask outright**: SSGI's +0.77 became +1.71, and the full-resolution attachment the first draft had costed for albedo was never needed. Landed: the buffer, the albedo fix, a fourth colour attachment (a **thirteen-place sweep**, two of which hide -- the pass binds a *subset* and `layout(location = N)` counts the subset, and four callers set the shape before `BuildFrame` runs because probe captures happen first), and **the denoiser on the traced form only**. **9.13a settled the ~30 % drop the denoiser appeared to cost (ENGINE-NOTES 7aw): it costs nothing, and the *unfiltered* figures were a third too high because a stochastic renderer measured through the tone curve reads high.** check_gi gained three claims and a third fixture for it. **9.13b landed the second bounce (7ax): `TraceReflection` split into a describer plus a four-line composer, `RenderSettings::GiBounces` 1\|2, and the pre-split and post-split builds render the same image to the byte at 1.** Open: **9.13c closed SSGI's feedback loop (7ay): the lit shader publishes what it added and the gather subtracts it off every tap, taking the bleed from +1.71 to +1.27 -- a quarter of that number was the gather reading its own output, and it was never one bounce. It also corrected 7av: the +16.98 was mostly a *unit error* (the screen-space chain carries linear depth in alpha, and the denoise pass handed it on into a buffer the lit shader multiplies by), not the loop, so the split those two were given is undone and both forms end on one `GI denoise`.** **9.13d added `PostSettings::GiQuality` (7az) and *filed* the probe fallback rather than building it: measuring what the probe already contributes showed the environment is counted twice -- the GI term is added on top of the probe and a missed ray returns the sky, so on a sun-less grey-sky room the far wall goes 171.0 to 214.7 with traced GI on. Filling missed taps with the probe would add a third helping. Closing that means the GI term replacing the environment's contribution rather than adding to it, and re-measuring every calibrated number; it is its own item.** 9.13 is otherwise complete. Also from this work, two findings about checks rather than about GI: **check_gi's thresholds were all floors**, so a bleed ten times too strong printed OK; and **a fourth claim was written, passed, and then survived three breaks of the thing it measured**, so it was deleted rather than kept. The other check scripts have not been audited for either shape |
| 9.14 | The GI term replaces the environment instead of adding to it | M | **DONE 2026-08-18, ENGINE-NOTES 7bb. Smaller than the sketch below: a traced miss contributes nothing (was: the sky, added to a probe that had already integrated it), and the screen-space gather counts every tap in its normalisation so it never extrapolates the two taps that found a wall over the ten that found nothing -- which is also 7av's probe fallback, done with nothing to add. On the new `gi_skylit` fixture the traced far wall goes 1.146x to 1.066x; the traced black-sky numbers do not move; the screen-space near bleed drops +1.27 to +0.22, because most of the +1.27 was extrapolation. `check_gi` claim 12 and re-measured bands.** The original finding: The lit shader adds the GI term *on top of* the reflection probe's irradiance, and a traced ray that misses returns the sky -- so environment light is counted twice. Measured on `gi_corner` with the sun off and a grey sky, where every photon on the far wall is environment light and nothing else: **GI off 171.0, ray-traced GI on 214.7**. It is why 7av's probe fallback for off-screen screen-space taps is filed rather than built -- filling missed taps with the probe would add a third helping in exactly the directions it already answered for. Closing it needs the lit shader to know what *fraction of the hemisphere* the estimate actually covered, both forms to report it (the traced form can count missed rays; the gather already tracks its accumulated weight), and the ambient term to blend probe against estimate by that fraction rather than summing them. **Then every calibrated number in `check_gi.py` has to be re-measured**, because all of its bands are fitted to the double-counted values -- which is why CHK.1 wants doing first |
| 9.15 | The screen-space gather, calibrated against the two world-space forms | M | **Specified, not landed, 2026-08-19 (ENGINE-NOTES 7bd).** The finding it exists for: on `gi_corner` at intensity 2 the screen-space form reads **+0.22** levels of red where voxel GI reads +1.78 and the traced form +1.86 -- sub-visible, and only checkable at all since 8.1 gave the comparison a world-space reference that runs on both backends. The named cause was the gather's `1/(1+d^2)`, which multiplies the numerator and not the weight. **It was deleted and measured, and the fixture rejected it**: +0.22 became **+4.17**, 2.2x the traced form, so the change is reverted and the entry records why. The falloff is not gratuitous -- this gather places every tap at the *same world radius* and reads whatever the depth buffer shows behind it, so tap density is uniform over a shell rather than over solid angle and nothing else in the estimator carries the form factor. Moving the inverse-square into the weight instead does not work either: 7bb's normalisation counts every tap's cosine, hit or miss, so that the estimate is the walls' share of the hemisphere, and `cos/d^2` is not comparable between a hit and a miss. What is left is narrow and costed: **sample directions and march to the first hit** -- `ssr_resolve`'s DDA from 9.10 is the machinery -- so the cosine weighting is right without a distance term. Target on the record: land between +1.78 and +1.86, keep +0.00 at the far end and off screen, backends inside 0.05. Two things already confirmed by the failed attempt: locality is `GiRadius`'s doing and not the falloff's, and the off-screen discriminator does not depend on it either |


### 9.0, done — and what the owner changed about it

Requested 2026-08-14, after a session in which the same settings turned out
to live in three places at once. **Built the same day, to a revised design.**

**Where they lived, and why that was wrong.** Exposure, bloom, shadows and
anti-aliasing were fields on `SceneEnvironment`, serialized into every
`.rage` — so they were **per scene**. The anti-aliasing *mode* was also
written to `ragev.ini` as a machine-wide override. Neither is where they
belong: a project's look is a property of the project, not of whichever
scene happens to be open, and duplicating exposure across forty scenes means
changing it forty times.

**The owner's revision, before any of it was built.** This section originally
called for two *sparse* profile assets, `.rvrenderprofile` and
`.rvpostprofile`, layered over project defaults:

> Render setting should be per project basis (and no I am dropping the idea
> of render profile) and post profile should be optional and it should be an
> asset that can be attachable to camera component.

Both changes remove work, and the second removes a class of bug. A sparse
layer needs a per-field "is this set?" bit, a UI that renders "inherited"
distinctly from "happens to equal the default", and a merge step — this
section used to call that the part to design for. A profile *attached to a
camera* needs none of it: there is nothing underneath to inherit from, so
every field a profile holds is a value it means. Sparseness was only ever
forced by the layering.

**What was built.**

| Home | Holds | Read by |
|---|---|---|
| `.rvproject` → `RenderSettings` | AA and its three parameters, shadows | The frame graph, once per frame |
| `.rvpostprofile` → `PostSettings` | Exposure, bloom, **and all of 9.1–9.7** | The camera that names it |
| `.rage` → `SceneEnvironment` | Ambient, sky, sky rotation, sky texture | The scene pass |

    render:  project        →  ragev.ini / --aa=
    post:    engine default →  the camera's profile, if it has one

The post chain has no project layer on purpose: "no profile" already means
something definite — the struct's defaults, which is what every scene
rendered with before — and a project-wide post block would be a second
answer to the same question.

On the camera rather than the scene because a grade describes a *view* and a
scene can hold several; the editor already renders two at once. It is also
where a post **volume** would write when it blends one grade into another,
which answers the open question this section used to carry about volumes by
putting the field where a volume would reach it.

**The part that decided whether it was maintainable.** All three blocks are
read and written through their registry, by one pair of functions in
`Scene/FieldSerializer`. The four hand-written lists this replaced are where
`TemporalFeedback` went missing — registered, inspectable, serialized
nowhere, so it reset on every load. The checks enforce the shape rather than
a list: set every registered field to a non-default value, round-trip,
compare. A field the writer cannot carry fails that, whatever it is called.

**What happened to the scenes already on disk.** Scene version 6. A
version-5 scene still carries the moved keys and the loader still reads them
— but only to *report* them, naming each and where it went. Migrating would
mean a scene load silently editing the project file or minting an asset;
dropping is what `TemporalFeedback` did. Only keys whose value differs from
the default are named, so the generated test scenes do not produce a warning
that means nothing — and `Knockdown/Main.rage`, which stores the full block
at its defaults, loads silently.

**One thing outside the engine had to change.** `make_aa_scene.py` and
`make_motion_scene.py` write `BloomEnabled: false`, because bloom spreads a
bright edge across exactly the pixels those checks measure. After the move
that key was inert. Both generators now write a `.rvpostprofile` beside the
scene, with its own `.meta` so the handle is fixed rather than minted on
first scan, and point the camera at it — the same path a person uses,
exercised on every run. `check_taa_motion.py` reproduces its recorded numbers
to three decimal places through the new path, which is what says the move
changed no pixels.

**A stale threshold this surfaced, in a different check.** `check_smaa.py`
required TAA to be 1.5x better than no filter on a static scene. That number
was measured when the feedback default was 0.9; 7.10 moved it to 0.6 and
re-calibrated `check_taa_motion.py` but not this one, so it has been failing
at two of four angles ever since on a build working exactly as intended.
Re-measured at 0.0, 0.6 and 0.9 and set to 1.25, which every angle clears at
the shipped default and every angle fails with accumulation switched off.
The same lesson §7r wrote down about itself: a threshold is only worth having
at the setting it actually runs at.

**Ordering, if it matters:** 9.3 then 9.1 gives most of the visible "graded"
look for very little. 9.2 changes how everything else is judged, so it belongs
before the expensive items rather than after.

---

### Phase 8 — Formerly out of scope *(reopened; read the cost before starting one)*

These were §7 non-goals, each named as "a place a solo engine dies". They are
future phases now rather than exclusions — but the reasoning that excluded them
was not wrong, and it is kept here as the price of admission rather than
deleted. **None of these should be started because it sounds interesting.**

| # | Item | Size | What it costs |
|---|---|---|---|
| 8.1 | Global illumination — SDFGI or voxel GI | XL | **Voxel GI done 2026-08-19 (ENGINE-NOTES 7bc).** The world-space form: the scene rasterised each frame into a clipmap of 3D textures around the camera (three draws a cascade, one per axis, `imageStore` from the fragment stage -- the RHI learned storage images for it), lit from the shadow cascades and the local lights, mipped **per viewing direction** so a thin wall stays opaque at every level, and cone-traced from every pixel as the head of the chain SSGI owns -- the third writer of the one `Indirect` buffer. `RenderSettings::VoxelGlobalIllumination` with resolution, cascades and voxel size; the profile's toggle stays the on switch; rays win where they run. **Both backends, no ray hardware**, and measured against the traced form: the red beside the corner +1.78 against +1.86, off screen +0.36 against +0.82 where the screen gather reads +0.00, 0.09 ms to rebuild the grid and 0.9 ms to gather at 1600x900. `GiBounces` 2 lights the grid from last frame's grid, converging on every bounce. **`check_gi` now runs end to end and passes (2026-08-19), which it had not when this row was written** -- and it found one thing: the voxel path is not byte-reproducible on **either** backend (Vulkan 1 level over 0.67-1.18% of channels, OpenGL 1-2 over 0.68-2.08%, measured by check_gi across five runs; the screen-space chain is 0 on both), so claim 15's radius test is measured against that floor rather than against zero. **The cause is open and three candidates are eliminated in 7bc** -- the voxeliser's write order, the temporal accumulation's start frame, and anything shared with the screen-space chain, which is 0 on both backends. Not done, stated in 7bc: skinned casters, conservative rasterisation, local-light shadows in the injection (**unguarded: `gi_corner` has no shadowed surface for the break to light, so claim 14 cannot see it**), the ray-query injection under traced shadows, the SDF, and the hybrid -- the traced second bounce reading the lit grid |
| 8.2 | Bindless resource binding | L | **Done 2026-08-16 (ENGINE-NOTES 7al).** The price turned out smaller than predicted: the split is forced in two places — the heap and the GLSL that indexes it — and neither is the RHI. The Vulkan lit pass reads material textures through a heap; OpenGL keeps the bound path, unchanged. One fork, in `Material` and one shader block, checked by rendering the demo both ways on Vulkan to zero differing pixels — which on its first run found the bound path drawing every wall at the plinth's tiling |
| 8.3 | GPU-driven rendering — compute-built draw commands, meshlets | XL | Real wins at hundreds of thousands of objects. The renderer draws a thousand in 1.9 ms and is nowhere near the wall |
| 8.4 | Terrain | L | **Stage 1 done 2026-08-17 (ENGINE-NOTES 7ap):** a heightfield as a *source of meshes* — an `.rvterrain` asset of 16-bit heights, a `TerrainComponent` (size, height, one material, collision), chunks of 64 quads at four levels of detail with skirts on shared edges, the level chosen per chunk per frame and read by the draw, the shadow casters, the ray-instance list and the picker alike; Jolt's height-field body over the same samples and the same triangle split, so a ball rests on what is drawn. The renderer never learned the word. `experiments/terrain/Chunk` is **kept, cut off** (owner's call): it was already in no target, and its `PerlinNoise` link is gone. **Stage 2 done 2026-08-17 (7aq):** up to four `.rmat`s in proportions painted into the asset (RGBA8 per sample after the heights), a `LayeredMaterial` bound as set 1 of a third lit pipeline, one `SampleSurface` fork in the lit shader; three maps per layer on both paths, both compared to the pixel. **Stage 3 done 2026-08-17 (7ar):** the brush the owner asked for — raise/lower, smooth, flatten and paint the four layers with one circular brush (size, strength, hardness), one stroke = one undo, per-chunk lazy rebuild, written back on save, `--brush=` for the checks. **Stage 3b done 2026-08-17 (7as), the owner's ask:** brush *varieties* — the kernel is now a shape (disc, or any greyscale mask from the editor's brushes folder, rotated, optionally following the stroke) times a pattern laid over the ground (noise, or a tiled mask), plus four more operations: terrace, ramp, set height and droplet erosion. Fifteen masks ship, all landforms or ground textures. **Stage 3c done 2026-08-18 (7au):** the last item off every stage's list -- **the ground under a point, from a script**, in C++ and C# (protocol 9). One implementation on `Scene`; the two script surfaces forward to it. It answers *whether* as well as *how high*, because `Terrain::HeightAt` clamps to its own extent and a float alone would report the rim's height for a point well off the edge. World in, world out, following the terrain's transform; where two terrains cover the point the higher surface wins. |
| 8.5 | Navigation and AI — navmesh generation, pathfinding | XL | An engine-sized subsystem on its own |
| 8.6 | Networking / multiplayer | XL | Touches every system that owns state. Retrofitting it is the classic way an engine's architecture is rewritten |
| 8.7 | Non-Windows platforms | XL | The RHI keeps the door open. Walking through it means a second window layer, a second input layer, and a second CI |
| 8.8 | XR | XL | Needs stereo rendering, a second projection path and device SDKs |
| 8.9 | FBX / Collada import | L | glTF covers Blender, Maya, Substance and every online library. This is a second material translation with its own failure modes |
| 8.10 | Visual scripting | L | **Done 2026-08-20 (ENGINE-NOTES 7bh), stages 1-5.** The canvas, the `.rvgraph` asset and the generator are built, and the generated C# **compiles against the real ScriptCore with 0 warnings**. Reading the base class corrected two things the design got wrong: the event is `OnTick`, because `Script` has no `OnUpdate` at all and a node whose label does not match the method it writes is the drift this design exists to avoid; and strict pin types left most of the node set unreachable, because the named field API is text, so a Bool, Float or Vec3 may widen to a String input while the reverse stays refused. A value read twice is spilled to a local, which the entry promised and the first cut did not do. The original objection stands and is the reason for the design: *"two scripting languages is already two — and this would be the third."* Every word of that is about a third **runtime** — a third set of interop bindings, a third surface to hold at protocol parity, to document, to check. **None of it is about a third editor.** So the graph is an *authoring surface*: a `.rvgraph` emits ordinary C# into `Scripts/Generated/`, and the existing managed pipeline compiles and hot-reloads it. No interpreter, no bytecode, no new execution path; live reload, the build console and restart-on-build all come free, and a graph is debuggable and diffable as C#. `rvgen` is the precedent. Three findings shrank it from XL: `ManagedScriptComponent` holds only a `ScriptName`, so attaching a graph costs **nothing**; the SDK-style `.csproj` already globs `**/*.cs`; and `UI::CurveEditor` (283 lines) is a working hand-rolled-canvas precedent. **Started because it is enjoyable, which this phase warns against — recorded rather than dressed up as necessity.** The hard parts are the canvas and keeping the v1 node set closed |
| 8.11 | Asset store / plugin ecosystem | XL | Needs a stable ABI, which nothing here has |
| 8.13 | Hybrid GI — rays for the first bounce, the voxel grid for the rest | M | ❌ **built 2026-08-19, removed 2026-08-20 (ENGINE-NOTES 7be).** Shipped, measured, and withdrawn the next day because it was strictly dominated by a feature the engine already had. On `demo` at 2560x1600, render-graph GPU time: one traced bounce **6.638 ms**, a second traced ray **9.208 ms** (+2.57), the hybrid **62.441 ms** (+55.80) — **21x the ray it existed to replace**, for four fifths of its quality (1.80x against 2.25x). The frame went 6.82 → 63.17 ms, and the owner hit 82-90 ms in the editor, which is what started the investigation. **This row previously claimed "0.09 ms, less than the rays it replaces". That was the *grid build*; the gather was never timed at all** — the whole item rested on a cost asserted from a design rather than measured, which is the rule 7be now carries. The cause was measured rather than reasoned: the cone gather ran once per ray hit from **incoherent origins**, which is a different cost class from the voxel form’s own gather off the g-buffer (6 cones, half res, under 0.1 ms). A fix exists and is written up in 7be — store the injection’s existing per-voxel `GatherCones` in a second volume and read one texel at the hit, measured at +0.045 ms — but it would be cheapest *and* worst, so it was not built. Rays and the voxel grid are exclusive again, as every other pairing in this renderer already is |
| 8.12 | Ray tracing — acceleration structures, ray queries, reflections and shadows | XL | **Done 2026-08-16, three stages the same day (ENGINE-NOTES 7am, 7an, 7ao):** acceleration structures in the RHI, ray queries proven in scenetest, and ray-traced shadows for **every** light kind behind one `RenderSettings::RayTracing` checkbox — hard-edged, bias-free, one ray per light per pixel, no cap on how many lights cast, skinned casters posed in compute and their structures refit each frame; checked against the maps to IoU 0.94–0.98 for the sun, a spot, a point light and the running fox. Then hit shading — buffers by address, a ray-instance table indexed by the instance's custom index, materials through the bindless heap — and with it **ray-traced reflections** in the lit shader where SSR's radiance enters (exact to 0.01 levels on 9.9's fixture and reflecting a block off the top of the frame that SSR cannot) and **ray-traced ambient occlusion** as SSAO's first pass (seam 15.17, brick wall at 1.000), each behind its own option under the checkbox, off by default, with the post profile's SSR/SSAO rows greyed and noted while the traced form runs. The whole block is offered only on a device with ray queries and under Shadows; OpenGL never shows it and takes the maps. Stated limits: hit shading is Lambert + emissive with a sun shadow ray only; rough surfaces keep the probe; no penumbra; RTAO is twelve rays through SSAO's blur, no accumulation |

**The honest ordering, if any of these happen:** 8.4 and 8.9 were the two
ordinary features; 8.4 is done, so **8.9 is the only ordinary one left** and
every other open row is XL. Open, as of 2026-08-20: 8.3, 8.5, 8.6, 8.7, 8.8,
8.9, 8.10 and 8.11 -- eight of the thirteen. 8.1 landed on 2026-08-19 and
8.13 was built and withdrawn the day after. 8.2 was a decision about the engine's identity, and it has been
made: the seam is at the shader define and the material, the RHI stays one
interface, and OpenGL is not dropped. 8.12 followed it and is done. The rest
are each larger than everything built so far.

### 8.12 in more detail, because it is the one most likely to be started for the wrong reason

*Kept as written before it was built; the plan below is what happened,
with two amendments recorded in ENGINE-NOTES 7ao: the reflection is not
"SSR with a traced fallback" but a traced mirror ray in the lit shader with
the SSR passes off, and the checkbox is a Render Settings row rather than
a mode enum.*

**What it would buy, which is more than 8.1 would.** The limits it removes are
already written down: ENGINE-NOTES 7af and 7ag record that screen-space
reflections only reflect what is on screen, arrive a frame late, and get no
help from the hi-Z pyramid on grazing rays; §7ac records that SSAO's occluder
is the depth buffer, which is why 9.8b needed an agreement rule; and cascaded
shadows carry acne against peter-panning with no setting that has neither. A
traced ray has none of those problems. That is a stronger case than 8.1, where
the roadmap's own note says the visible delta is small.

**Two tiers, and the distinction is the whole plan.** *Ray queries* trace from
an existing compute or fragment shader — no new pipeline type, no shader
binding table — and the passes that would use them already exist. *Ray tracing
pipelines* add raygen/miss/closest-hit/any-hit stages and an SBT, which is a
genuinely new concept in the RHI. The staged version is acceleration structures
plus ray queries, wired into the SSR trace as the fallback when the
screen-space walk misses, judged against the exactness fixture 9.9 already
built.

**What it costs regardless of tier:** a new RHI resource class (a bottom-level
structure per mesh, a top-level one per frame, with build and refit paths — the
largest single addition to the RHI since it was written); a BLAS refit every
frame for anything skinned; device-feature gating, for which the caps
mechanism already exists and the "skipped on this backend" check shape is
already demonstrated by `CheckParticleSort`; and an RTX or RDNA2-class GPU to
develop against with a defined behaviour on everything older.

---

### Phase 10 — Engine health *(no new capability; the engine gets better at being one)*

Every row above adds something the engine could not do before. **None of them
make what is already there smaller, faster, or harder to break** — and by the
end of phase 9 that had become the larger risk. 8.13 is the case in point: it
shipped on a cost figure nobody had measured, through a check suite that could
not tell a black frame from a zero, on a renderer whose shaders the build never
verified. Not one of those is a missing feature.

So this phase has no theme beyond **the engine being trustworthy to work on**.
Rows are earned by evidence rather than proposed by taste: a check that cannot
fail, a measurement that was never taken, code nothing reaches, a cost nobody
looked at. **A row here must name the thing that went wrong, or the number that
was never measured** — "tidy up the renderer" is not a row.

Unlike phase 8, these are meant to be picked up in gaps rather than planned:
most are S, none block anything, and any of them can be done on the way past.

|  # | Item | Size |  What it is |
| ---|---|---| ---|
| 10.1 | The three verification holes | S | ✅ **done 2026-08-20 (ENGINE-NOTES 7bf).** All three let a completely broken renderer report success, and all three were found the hard way building 8.13. **(a) Shaders compile at runtime, so a green `cmake --build` proves nothing about them** — 8.13's lit shader had a bad `#include` from the moment it was written, never compiled once, rendered every `gi_*` fixture pure black, and the runtime exited 0 throughout. `ShaderCompiler` now counts failures and a `--screenshot` or `--benchmark` run exits 3. **(b) `check_gi` read +0.00 off 27 blank frames and called it a measurement**; `rvcheck.require_drawn` refuses a frame nothing was drawn into. **(c) The runtime loads `assets/` beside its exe, not the source tree** — three timing variants were measured off shaders that never ran; `rvcheck.require_current_shaders` refuses a stale tree, and every check that launches the runtime calls it. A fourth hole fell out of fixing (c): `falsify.py` now marks the active break, so a forgotten `restore` can no longer be mistaken for a clean run. All four falsified |
| 10.2 | Dead code and settings nothing reads | S | Not a tidy-up: **a setting the engine ignores is a lie the inspector tells**, and the manual's drift check will happily document it. Wants a pass over `RenderSettings`/`PostSettings` for fields no resolve function reads, registry rows whose predicates can never be true, and RHI entry points with one caller that is a test. Evidence first — name each one and what reaches it |
| 10.3 | The costs never measured | M | 8.13's rule generalised: **a performance claim is a measurement or it is not a claim.** The profiler has GPU timestamps per phase and almost nothing quotes them. Wants a standing benchmark fixture and a recorded per-phase baseline, so a regression is visible as a number rather than as somebody noticing the editor feel slow. Note `--frame-time` makes the CPU frame column a constant and must never be passed to a benchmark |
| 10.4 | The claims that have never failed | S | 7ba's shape 3, still open in places: `voxel-no-shadow` guards nothing, and claim 15's two attempted breaks both survived and were deleted. Every claim wants a break in `falsify.py` that has been seen to go red, and the ones that cannot have one want a sentence saying why |
| 10.5 | Minor improvements found in passing | S | The catch-all, and deliberately the lowest priority here. Anything small, evidenced and unblocking: a confusing tooltip, a log line that says nothing, an editor row in the wrong section. **Not a licence to refactor** — if it needs a design, it needs a row of its own |
| 10.7 | Visual scripting reaches the scripting API | M | ✅ **done 2026-08-20 (ENGINE-NOTES 7bh, 10.7).** The node set went from 18 to **99** across 16 categories, covering every entry point a C# script can reach — transform, hierarchy, physics, raycasts, input, time, audio, components, UI and the maths library — plus **variables**, generated as fields so they survive between events. That last one is the only *language* construct added, and it is the one that mattered: v1's own check fixture had to teleport a cube because a graph could not remember a number between two ticks. Loops, user-defined functions and collections are still absent and are listed as absent — a node is the wrong shape for them. The emit rule moved onto the descriptor beside the pins, so a node is one line of a table rather than a case in a 99-way switch, and the table comes off one spec so pins and generated C# cannot be edited apart |
| 10.6 | Every GI setting in one place | S | ✅ **done 2026-08-20 (ENGINE-NOTES 7bg).** `GiBounces`, `VoxelGlobalIllumination` and the three voxel dials moved from Render Settings into the post profile's GI section, so that when ray-traced GI takes over **every** rasterisation GI row greys out together instead of in two panels. `GiBounces` had to move as well, and not for tidiness: a registry predicate is handed the block its row belongs to, so a render row whose visibility depends on a profile setting would have to cast the wrong struct or acquire a global. **Amends the 9.0 rule** that the profile owns the look and Render Settings owns the cost — `GlobalIllumination` costs four passes and `GiQuality` changes the gather's resolution, and both were always profile settings, so the line actually drawn is the *hardware* budget. The accepted consequence is stated rather than dissolved: a camera cut between profiles can change the ray budget. An old `.rvproject` naming the moved keys is warned, not migrated and not silently dropped (the 7s rule) |

**What does not belong here.** Anything that changes what the engine can do —
that is a phase 8 row however small it looks. And anything justified by "this
would be cleaner": the whole point of the phase is that its rows come from
something having gone visibly wrong, and a row without that evidence is a
preference wearing a number.

## 6. Technology choices

| Need | Choice | Reasoning |
|---|---|---|
| glTF import | **fastgltf** | Spec-complete glTF 2.0, C++17, SIMD-accelerated, minimal deps, MIT. Assimp is a large dependency that mostly handles formats we've decided not to support. fastgltf deliberately doesn't decode images — `stb_image` is already in tree for that. |
| Physics | **Jolt** | Modern, multithreaded, deterministic, MIT, shipped in *Horizon Forbidden West* — and **Godot made it their default in 4.6**, which is the strongest possible endorsement given Godot is the scope benchmark. PhysX 5 is BSD-3 now but the API is heavy and NVIDIA-shaped; Bullet is the incumbent by inertia. |
| Audio | **miniaudio** | Single-file, public-domain-equivalent, zero dependencies, built-in 3D spatialisation and node graph. OpenAL Soft is LGPL (dynamic-link constraint on a packaged game). FMOD cannot be redistributed — every user would need their own licence, which is disqualifying for an engine. |
| Reflection | **`entt::meta`** | Already vendored with EnTT. No new dependency for the highest-leverage item in Phase 0. |
| C# host | **CoreCLR via `nethost`/`hostfxr`** | Mono's embedding API is friendlier but Mono is legacy. .NET 8+ gives collectible `AssemblyLoadContext` (real hot reload) and `[UnmanagedCallersOnly]` + function pointers (near-native interop without `mono_add_internal_call`). Note the constraint: **once loaded, the runtime lives until the process dies** — reload means swapping load contexts, never the runtime. |
| Render graph | **hand-rolled** | Small, and it needs to know this RHI's barrier model. Not a dependency. |

---

## 7. Scope

There is no longer a list of permanent exclusions. What used to be §7's
non-goals are **Phase 8**, reopened at the owner's direction, with the reasoning
that excluded them kept beside each item as its cost rather than thrown away.

Two of them are worth repeating here because they are not merely large:

- **Bindless (8.2) was a decision about what this engine is, and it is made.**
  OpenGL 4.5 has no equivalent to Vulkan's descriptor indexing; the engine
  keeps both by forking at the shader preprocessor and the material, never at
  the RHI (ENGINE-NOTES 7al). Everything else in this document is compatible
  with both, and 8.12 was the first item that is not: ray tracing is Vulkan
  only, offered in the panel only where the device traces, and OpenGL keeps
  the maps and the screen-space effects.
- **Terrain (8.4) has a false start in the tree, kept on purpose.**
  `experiments/terrain/Chunk` creates one entity per voxel face. Stage 1 of
  8.4 was built beside it, not on it (ENGINE-NOTES 7ap), and it stays in
  `experiments/` as the shape to avoid.

The bar from §2 still applies to all of it: an item is done when it works on
both backends, exits cleanly, and has been looked at as pixels rather than as a
draw count.

---

## 8. Bugs and debts found during this pass

New findings, not in HANDOFF:

- **`NativeScriptComponent::Bind` captures `this` by reference.**
  `OnInstantiateFunction` and `DestroyInstanceFunction` are `[&]` lambdas that
  capture the enclosing component's `this`. EnTT relocates components when
  storage grows, so those lambdas dangle as soon as another entity gets a script.
  Use the component-passed-as-parameter form instead.
- **`OnDestroyFunction` and `DestroyInstanceFunction` are never called.**
  Nothing in the codebase invokes them. Every script instance leaks, and
  `OnDestroy()` never fires. `Scene::~Scene` is empty.
- **`Scene::OnUpdate` both simulates and renders**, with no edit/play
  distinction — scripts run continuously in the editor. Splitting this is
  item 2.1 and is a prerequisite for play mode.
- **Vendored EnTT is 3.10.0, stored as a 5 MB UTF-16LE single header** — it is
  not a submodule like the other eleven dependencies, and the encoding defeats
  ordinary text tooling (grep finds nothing in it). Worth normalising to UTF-8
  and pinning as a submodule when Phase 0.4 touches it anyway.

Carried over from HANDOFF and now dated by phase: fake entity IDs (0.2), no
hierarchy (0.3), `Camera.h`/`Cameranew.h` duplication (0.1), legacy `.glsl`
files (any time), `Scene::DeleteEntity` taking `Entity&` (0.7).

---

## 9. Recommendation

*The original recommendation — do Phase 0 completely, then Phase 1, and pull
Phase 3 forward only for a visible milestone — was followed, and it held. Phases
0-4 are done and Phase 3 is complete. What follows replaces it.*

**Do Phase 5 next, and finish it.** It is the last MVP+ item and the only one
left that the roadmap's dependency argument still applies to: C# has to mirror a
native surface that has stopped moving, and the native surface stopped moving
today. Every month it waits is a month of API drift it will have to absorb.

**Then build a game with it, before Phase 6.** This is the strongest
recommendation in this document. Phases 6-8 are a menu of forty-odd items with
no dependency order, and the only reliable way to know which ones matter is to
need one. An engine improved from the inside grows features nobody asked for;
an engine improved from a game grows the ones that were in the way.

If a phase must be picked without that: **Phase 6**. Text and UI are the only
remaining gap a *player* would notice — everything else on the list is something
a developer would notice. An engine that cannot draw a score is not finished in
a way that matters, however good its shadows are.

**Phase 8 items should each be treated as a proposal, not a task.** Every one of
them was excluded once, with a reason that is still recorded next to it. Two of
them — bindless and terrain — had prerequisites that were decisions rather than
work, and both decisions have been made.
