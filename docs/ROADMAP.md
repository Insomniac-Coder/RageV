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
| Post chain | bloom, DOF, SSAO, SSR, TAA, FSR2 | ❌ none | HDR target, tonemap, bloom, FXAA |
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
| 3.2 | HDR target + tonemap/bloom post chain (moves ACES out of the PBR shader) | M | ✅ plus FXAA |
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
| 7.8 | Front-to-back depth sorting for opaque draws | S |
| 7.9 | SMAA | M |
| 7.10 | TAA — needs motion vectors first, which also buy motion blur and upscaling | L |
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

### Phase 8 — Formerly out of scope *(reopened; read the cost before starting one)*

These were §7 non-goals, each named as "a place a solo engine dies". They are
future phases now rather than exclusions — but the reasoning that excluded them
was not wrong, and it is kept here as the price of admission rather than
deleted. **None of these should be started because it sounds interesting.**

| # | Item | Size | What it costs |
|---|---|---|---|
| 8.1 | Global illumination — SDFGI or voxel GI | XL | IBL plus good shadows already gets most of the perceived benefit. This is the largest possible effort for the smallest visible delta in this engine |
| 8.2 | Bindless resource binding | L | **The first place the two-backend commitment has a real price.** Descriptor indexing is core in Vulkan 1.2; OpenGL 4.5 has no equivalent. Doing this means either dropping OpenGL or maintaining two materially different binding models |
| 8.3 | GPU-driven rendering — compute-built draw commands, meshlets | XL | Real wins at hundreds of thousands of objects. The renderer draws a thousand in 1.9 ms and is nowhere near the wall |
| 8.4 | Terrain | L | **`experiments/terrain/Chunk` must be deleted first.** It creates one entity per voxel *face* — thousands of entities per chunk — and is not a starting point for anything |
| 8.5 | Navigation and AI — navmesh generation, pathfinding | XL | An engine-sized subsystem on its own |
| 8.6 | Networking / multiplayer | XL | Touches every system that owns state. Retrofitting it is the classic way an engine's architecture is rewritten |
| 8.7 | Non-Windows platforms | XL | The RHI keeps the door open. Walking through it means a second window layer, a second input layer, and a second CI |
| 8.8 | XR | XL | Needs stereo rendering, a second projection path and device SDKs |
| 8.9 | FBX / Collada import | L | glTF covers Blender, Maya, Substance and every online library. This is a second material translation with its own failure modes |
| 8.10 | Visual scripting | XL | "Two scripting languages is already two" — and this would be the third |
| 8.11 | Asset store / plugin ecosystem | XL | Needs a stable ABI, which nothing here has |

**The honest ordering, if any of these happen:** 8.4 and 8.9 are ordinary
features. 8.2 is a decision about the engine's identity, not a feature. The rest
are each larger than everything built so far.

---

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

- **Bindless (8.2) is a decision about what this engine is.** OpenGL 4.5 has no
  equivalent to Vulkan's descriptor indexing, so adopting it means dropping a
  backend or maintaining two different binding models. Everything else in this
  document is compatible with both.
- **Terrain (8.4) has a false start in the tree.** `experiments/terrain/Chunk`
  creates one entity per voxel face. It must be deleted before anything is
  built, not extended.

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
them — bindless and terrain — have prerequisites that are decisions rather than
work.
