# RageV — the one list

**Read this before picking up work.** Written 2026-08-27, because there were
three roadmaps running at once and no single place that said what to do next.

This does not replace the three; it orders them. Each still owns its own
detail:

- `ROADMAP.md` — the engine's phases, the capability audit, and the *reasons*
  behind every scope decision. Phase 8 and phase 10 are the live ones.
- `BAKING-ROADMAP.md` — the five baking candidates, costed, and the pitfalls.
  Its ordering is spent: 1.1, 1.2 and 1.3 are all done.
- The **Golden Gate brief** (artifact, see HANDOFF) — the demo scene, and the
  engine gaps it forces into the open.

---

## Where the three tracks actually stand

**Baked lighting is feature-complete and being hardened.** Probes to disk,
the runtime-filled volume and the baker on disk (BAKING-ROADMAP 1.1–1.3) all
shipped. Since then: two flavours per lighting, multi-bounce solves, the
`Is Baked` light flag, `Auto Fit` volumes, BC6H probe compression, and the
on-plane-cell defects. What is left is not the roadmap's list — it is the
list this work generated.

**Phase 8 has no ordinary features left.** 8.5 (navigation), 8.6
(networking) and 8.7 (non-Windows) are the only open rows and all three are
XL commitments about *what the engine is for*, not gaps to fill. 8.8 is
archived and 8.11 deferred, both on the owner's call.

**Phase 10 is nearly closed**, with 10.5 — the evidenced catch-all — the only
row still open by design.

**So the real roadmap now comes from the demo scene**, which is what a demo
scene is for. The Golden Gate night scene forces five engine gaps into view,
and they are better rows than anything left in phase 8 because each one is
demanded by a picture somebody wants to make.

---

### ~~⭐ FIRST — a realism research session (owner-set, 2026-09-01)~~ — ✅ **done 2026-09-01**

**The deliverable is `docs/RENDERING-REVAMP.md`** — fourteen work items
(WR-0…WR-14), each a self-contained brief with named shipped precedents,
costs, backend tags, verification protocols and traps, written so an agent
can pick one up cold. Research base: four parallel literature strands over
GDC/SIGGRAPH primary sources + a completeness critic, the bridge's measured
lighting design (DOE PNNL-21894: 128 posts, 250 W HPS behind amber lenses,
graduated 150/250/400 W tower uplights), reference photographs, five
bake-matched captures of the current night scene, and a ReSTIR deep-dive.

The ranked order (rationale and details in the plan):
**WR-0** generator parity (the scene is hand-owned since `dab692d` —
regenerating DESTROYS the lamp radius/range work and the marine lights, and
silently invalidates the bake) → **WR-1** shaped sky + SF glow dome + moon
(the cheapest big win; probe/GI/fog/water all inherit it) → **WR-2** dither →
**WR-3** fog inscatter from the sky per view direction → **WR-4** measured
photometry & the missing fixtures (floods, beacons, nav lights) → **WR-5**
night glare structure (thresholdless bloom + Spencer PSF weights + lamp
flare sprites) → **WR-6** water sparkle fix (local bound replaces
ReflectionFloor 30 — mechanism owner-confirmed) → **WR-7** capsule lights →
**WR-10** world-space light grid at RT hits (kills the walk-every-light hit
loop AND the 16-emitter cap) → **WR-13+WR-8** specular AA + LTC line/tube →
**WR-9** luminaire binding + the four-path single-counting contract →
**WR-14** foam at night → **WR-11** froxel volumetrics (the one real new
system) → **WR-12** display finishers. Budget: ~2.5–3 ms of adds on the
12.6 ms night frame; target 60 fps at 2560×1600 held.

Rows 6 (area lights) and 7 (volumetrics) below are subsumed by WR-8 and
WR-11 respectively — the revamp plan is their spec now. Row 5 (LODs) stands
apart and unchanged.

**2026-09-02, fourth session:** the 1440p baseline is taken (HANDOFF's
agenda section: Headland 114 ms, the water pass is the frame), and
**WR-17** — shadow rays thinning with distance plus a light cutoff, render
settings driving every light — is built and measured in place of WR-8,
owner's call, and shipped as the **RT optimisation** preset (Off / Quality /
Balanced / Performance; the project still at Off). Next: pick the level,
the after-table per level, then WR-10 and WR-16.
Beyond the list, ranked from the numbers, in RENDERING-REVAMP's
"Frame-time candidates" section: refraction rays only where they can show,
render scale with temporal upsample, reflections out of the fragment
shader at half res, and the GPU-LOD design doc's one fit — a proxy BLAS
for far shadow rays through the instance masks the TLAS already carries.

**Late on 2026-09-02:** WR-18 built and measured (the water's rays: quads
and the refraction reach; Quality now 90 / 90 / 73 ms). Then the
measurement that reorders everything: the light walk at traced hits is
~40 ms, a third of the frame. **The single ordered plan for the ray side
is now WR-16, `docs/RAY-BUDGET-DESIGN.md`** — one controller, one
allocator, the consumers, ReSTIR on top (ReSTIR is no longer an item of
its own) — with WR-10 first (M1), the allocator and controller next (M2,
M3), ReSTIR DI as M4 behind the shadow interface.

**2026-09-03:** the **static / moving split** shipped (ENGINE-NOTES 7cx,
HANDOFF's start-here): a `Static` checkbox per mesh and per terrain, off by
default; the bake sees static objects only; static surfaces read fully
baked lamps from the field, moving surfaces take them live and still shadow
the static floor from them. Every existing scene was marked by
`tools/scripts/mark_static.py` (owner's call). **The same evening the
bridge's 176 lamps went Hybrid Full Bake at 2 m** (a fourth mobility: half
baked within a per-light radius, fully baked beyond) with three more boxes
over the bay and the shores and a packed atlas: **542 → 315 ms over the
eight cameras**, Hybrid costing 3% over pure Full bake. HANDOFF's evening
entry has the table and the two fast-because-wrong traps it took to get an
honest number. **Next is WR-16, decided and sequenced:**
`RAY-BUDGET-DESIGN.md` is one document in four parts now; Part IV has the
review, the owner's decisions and the order -- counters, the one-day
fixed-budget pre-check on the water, the cheap hit walk, the budget
(allocator then controller, always), and ReSTIR as the shadow spender,
built unless the pre-check finds structural bias and judged by the flicker
protocol under every AA. Start at S0.

**2026-09-04: S0 is built** (ENGINE-NOTES 7cy; RAY-BUDGET-DESIGN Part IV
"S0, built"): the rays are counted where they are cast and read back a
frame late, the report and the HUD say rays per frame by kind, lights per
fragment and temporal confidence, `--debug-view=` draws them, the ray
costs are calibrated into the project, and the Hybrid bridge has its
`wr16-before` baseline. The counting costs 1.4% and changes no pixel --
after a day in which its first build cost 26%, because a shader with a
side effect loses its early depth test. **The calibration's finding that
outranks the plan's expectations: the frame is light-bound more than
ray-bound** -- with every lamp's shadow ray removed, 70-80% of the frame
remains; the lamps' rays are 20-30%, the hit walk 13-22%, and the rest is
the shading of 77-125 lamps per fragment. The owner's third document (a
spatiotemporal reconstruction design) was judged against those numbers
the same day (RAY-BUDGET-DESIGN Part IV, "The third source, judged"):
its history validation, allocation order, shadow reconstruction,
half-resolution water rays and the moving-camera benchmark are taken into
S3, S4 and a new S5; its GI/AO reconstruction is not (under 2.5% of this
frame). **Next is S1**, now with two arms: the one-day fixed-budget
pre-check on the water -- 1 / 2 / 4 / 8 shadow rays per pixel by a cheap
importance, three AA modes, three cameras, diff images and the flicker
count -- first raw, then with the third source's temporal accumulation
behind it; the verdict is the ray count to build for, whether any lamp
group's shadow is structurally missing at 8, and how many rays a pixel the
accumulation lets the water hold.

## The order

Ranked by value per hour, not by size. Numbers are engineering judgement.

**What actually moves realism, ranked, after a night spent failing at it
(2026-08-31):** the *assets* first and by a wide margin — every object in the
demo is procedurally generated and every texture is a 1K CC0 set, and no
renderer feature compensates for that. Then **LODs** (5), because they are what
lets denser assets exist at all. Then **area lights** (6). Then **volumetrics**
(7). The renderer itself is not the gap: clustered forward with no light cap,
ray-traced shadows, reflections, AO, GI and water refraction, baked irradiance
volumes, five AA modes and a full post chain all run together at 129–212 FPS at
2560x1600.

### ~~0 · Alpha-cutout materials~~ — ✅ **done 2026-08-28**
`BlendMode::Masked` with an `AlphaCutoff` per material, glTF `MASK` importing
as itself, its own draw bucket, run and pipeline. Both backends. Verified end
to end by `tools/scripts/check_cutout.py`, which asserts *where* the hole is
rather than whether one exists — a cutout that removed the whole object would
pass a presence test just as well.

All four paths test the same alpha:

| path | mechanism | measured |
|---|---|---|
| lit | `discard` in a shader variant | half the surface, exact vs an Opaque control |
| depth prepass | excluded | no depth left behind the hole |
| shadow map | second depth pipeline with a UV and set 1 | 97 px vs 196 |
| ray query | test inside the traversal loop | 98 px vs 196 |

**Its own pipeline, and that is the point.** A `discard` present in a shader
costs early-z whether or not it is reached, so compiling the test into the
shared opaque shader would have charged every surface in the frame and undone
the depth prepass. Masked geometry is its own bucket; the opaque shader is
untouched.

**The ray half has no any-hit stage to write.** Ray queries, not a
ray-tracing pipeline: the decision happens inside `rayQueryProceedEXT`, which
was an empty loop. Only masked *instances* set
`VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE`, so everything else is still committed
by the hardware without reporting a candidate — that is what makes the known
cliff of a texture fetch inside traversal affordable. Per instance and not per
geometry, because a bottom-level structure is per mesh while being cut out is
per material.

**Still solid to ambient occlusion**, deliberately: that pass holds the
acceleration structure and nothing else, and plumbing the instance table, the
material table and the heap through it to refine a short-range darkening term
is not the trade. Occlusion under a railing is a little too dark; the shadows
and reflections, where a fence reading as a wall would actually show, do test.

**Four traps, each of which cost a wrong picture:**

- **`discard` at SPIR-V 1.6** lowers to an instruction with no OpenGL form.
  The whole fragment stage failed to cross-compile and every cutout drew solid
  black there while Vulkan was perfect. The compiler targets 1.5.
- **The shadow pass culls nothing**, so a closed *box* casts from its back
  face too, whose UVs run the other way in x — the two holes miss and the
  shadow comes out solid. Model cutouts as planes. A box's rim faces are not
  cut out either and cast a hairline across the gap.
- **A set is allocated against a pipeline.** The masked pipeline's layout is
  identical to the opaque one, but identical is not interchangeable: OpenGL
  resolves bindings against the program, and reusing the opaque set drew every
  cutout black there while Vulkan showed nothing wrong.
- **`gl_RayFlagsOpaqueEXT` overrides the per-instance flag**, so it had to go
  from any ray that can hit a cutout. The shadows-only shader variant, which
  has no instance table to read a cutoff from, keeps it — and pays nothing for
  a test it could not run.

Left undone, and cheap to add if a scene wants it: the alpha a shadow tests
comes from the *material*, not the instance, so an entity that overrode its
base-colour alpha would cast a shadow cut to the material's. Nothing here does
that, and it is written down in `shadow_depth_masked.rvshader`.

### ~~1 · Exponential height fog~~ — ✅ **done 2026-08-27**
One fullscreen pass over the depth buffer, 0.014 ms GPU, off by default.
Height fog with the density integrated in closed form along the view ray, so
it stays right with the camera inside the layer or above it; the sky is fogged
to full depth so a distant headland reads as distance rather than a cut-out.
Applied after occlusion and **before** depth of field and bloom, so a fogged
lamp blooms as the fog's colour instead of having grey painted over its bloom.
Seven fields on the post profile.

Not done: **volumetric shafts** (item 6) are still the separate, larger job.
This is the cheap half of the mood and it was always meant to come first.

### ~~2 · Reverse-Z depth~~ — ✅ **done 2026-08-27**
The near plane is 1 and the far plane 0, one convention engine-wide, stated
in `RHITypes.h` (`kDepthCompare`, `kDepthClear`) and referred to rather than
re-typed. Free: same buffer, same bandwidth, planes the other way round.

Measured on the D32_SFLOAT the scene already uses, at a 5 cm near clip
against a 4 km far plane — the smallest separation two surfaces can have and
still be told apart:

| | conventional | reverse-Z |
|---|---|---|
| 1 km | 43 cm | 0.03 mm |
| 2 km | **4.4 m** | 0.06 mm |
| 4 km | **23 m** | 0.12 mm |

So the prediction was right: at 2 km the conventional buffer cannot separate
surfaces four metres apart, and the deck and cables are closer than that.

Four things beyond the obvious flip, worth not rediscovering: the **shadow
depth bias is negated** (away from the light is down); the **point-light cube
shadow rebuilds the projection's depth by hand** and is the only place the
formula is repeated; **"past the far plane" tests invert** — left as `> 1.0`
they never fire; and **frustum extraction is unchanged**, because the clip
test is still `0 <= z <= w` — only the near/far labels swap.

Verified: scenetest green on both backends, `check_depth_sort` 0 of 4,320,000
pixels changed with early-z still rejecting 73x, depth of field and motion
blur unchanged to the byte, showroom max diff 8 levels (Vulkan) on
high-contrast edges only.

### ~~3 · Sky occlusion in the irradiance volume~~ — ✅ **done 2026-08-28**
Shipped as `2f87153`, "Carry sky visibility in the light tiles' alpha", and
left standing on this list for four days afterwards — which is how it came to
be quoted back to the owner as open work on 2026-09-01. Each cell stores how
much sky it sees; the fragment multiplies by the live sky colour at shade
time, so a bake follows the time of day instead of freezing it. It rides the
light tile's alpha lane rather than the visibility tile's `.y` as planned.
`BakedLighting::kVersion` is 3, so pre-`2f87153` bakes are rejected rather
than read as zero.

**Strike a row the day it lands.** A stale roadmap is worse than no roadmap:
it is confidently wrong, and nothing in a build catches it.

### ~~4 · Independent irradiance volumes~~ — ✅ **done 2026-08-27**
Volumes are no longer composed into one union box. Each keeps its own grid,
spacing and rotation, packed side by side into one 3D texture; the fragment
picks whichever volume it is *deepest inside* and reads that one. Verified:
`field_two` solves as "2 volume(s) in a 14x9x34 atlas" where it used to
produce a single merged grid, and the single-volume scenes are unchanged.

Three things worth not re-deriving. **They share one texture because they
must** — a fragment shader has 32 samplers on OpenGL and the layered terrain
variant already spends 31, so N volumes cannot be N bindings; volume *v*'s
tile *t* lives at slices `t · AtlasDepth + v.ZOffset`. **The sweep is the
outer loop and the region the inner one**, because the Jacobi swap that ends
a sweep flips the whole atlas — a region carried to its last sweep while its
neighbours sat at their first would leave theirs stale in whichever buffer
came to the front. And **the bake stamp gained a `Layout` hash**, because one
texture of a given size can hold any arrangement of boxes; `kVersion` went to
2, so pre-existing field bakes are refused and re-made.

Not done, and deliberately: **blending across an overlap.** A fragment inside
two volumes takes the deeper one outright rather than fading between them, so
a boundary between overlapping volumes can show. Fine for volumes that abut;
worth finishing before a scene leans on nested ones.

### 5 · Mesh LODs — ~3–5 days
No LOD chain exists and no simplifier is vendored. 2.7 km of repeated bridge
bays pays full triangle price forever. Import-time simplification plus a
distance selector is the systemic answer; hand-authored near/far variants get
one scene shipped.

**Promoted in practice by the night session (2026-08-31).** It is no longer
only a performance row: it is what caps how much geometry a scene may contain,
which caps density, which caps realism. Every asset in the Golden Gate frame is
procedurally generated at low density — the bridge is 105k triangles for a
2.7 km structure — and the reason is that nothing sheds detail with distance,
so everything must be cheap everywhere. Authored or scanned rock, which is the
owner's next move on the cliffs, is expensive for exactly this reason. **This
blocks the largest realism win there is, which is the assets.**

### 6 · Analytic area lights — ~1 week
**Every light in this engine is punctual.** `AreaEmitter` exists (Light.h) but
it is next-event estimation for the *traced* GI, capped at 16 — it makes
emissive geometry contribute to a bounce, and it is not direct specular.

Found the hard way on 2026-08-31, chasing the sodium lamps' glitter path on the
water and failing:

- **A point source cannot make a streak.** Its specular reflection is a delta —
  a wave facet returns light only where its normal exactly mirrors camera to
  light, which is a vanishingly small set of pixels. An area source is
  integrated over a *solid angle*, so many facets return something and the
  result is the continuous vertical smear every night photograph of this bridge
  is made of. The streak's length is the source's size convolved with the wave
  slope distribution. No brightness setting substitutes for extent, and two
  sessions' worth of work on the reflection path could not have produced it.
- **The tower floods blow out**, and were hand-tuned from a 12°/30° cone to
  26°/58° to hide it. That is papering over a punctual source: all its energy
  leaves one point, so the near field is always over-bright. An area source
  spreads the same energy and falls off up the shaft without a cone to tune.
- **Soft shadows do not widen.** A 0.6 m luminaire throws a penumbra that grows
  with distance from the occluder; a point throws a hard edge or a uniform
  blur.
- **The lens and the light are two objects pretending to be one.** The glowing
  luminaire is emissive geometry and the illumination is a separate spot, so
  they can drift — `bridge.lamp_light()` exists only to keep them together.
  With area lights the emissive quad *is* the light, and what is seen, what is
  lit, what reflects and what shadows agree by construction.

LTC (linearly transformed cosines) is the standard answer: analytic, two lookup
textures, roughly twice a punctual light's ALU. With 128 lamps already binned
into clusters and the night frame at 12.6 ms, affordable.

### 7 · Volumetric light shafts — ~1–2 weeks
Froxel grid, ray march, temporal reprojection. The effect that puts visible
cones under sodium lamps in sea mist. Do it *after* height fog proves the
appetite — fog delivers most of the mood for a tenth of the work.

**And the appetite is now proved.** Sea mist under sodium is most of the mood
in every reference the owner has brought to the night scene; height fog gives
the distance cue and none of the shafts.

### Not scheduled, and why

*(Alpha-cutout materials moved OUT of this section and to the top of the
queue on 2026-08-27, the owner's call — see HANDOFF's "next session's job".
The note below stands as the reason it was ranked low, and the correction:
there is no any-hit stage to write, because every ray here is a ray **query**,
so the decision goes inside the traversal loop instead.)*
- **Sparse brick volumes (APV-style).** The general answer to level-scale GI.
  Item 4 is the cheaper answer that fits a linear scene; revisit bricks when
  an open world needs one.
- **Lightmaps.** Permanently blocked: `MeshVertex` carries one UV set, so a
  second channel is a vertex-format change and a rewrite of everything
  downstream of it. BAKING-ROADMAP 1.4 said XL and would not; that still
  holds.
- **Phase 8's remaining rows** (navigation, networking, non-Windows). Each is
  a decision about the engine's purpose. None should be started because the
  list looks short.

---

## The standing rules this work produced

Written here because they are cross-cutting and easy to lose.

- **A performance claim is a measurement or it is not a claim** (10.3, and
  8.13's corpse).
- **Render comparisons are judged by per-pixel diff images, never by mean
  levels.** A hard line reads as a small mean; a harmless offset reads as a
  large one. The acceptance bar for baked-vs-realtime is near-zero visible
  difference, or a difference that favours baked.
- **The frame may guess; the bake may not.** A frame's estimate dies with the
  frame. Anything the solve reads is stored, and the next sweep treats the
  store as fact — so an unfounded read becomes a loop with a gain.
- **A volume must cover everything visible.** Geometry outside the box gets no
  baked light at all, and the boundary reads as a line across whatever crosses
  it.
- **Cells smaller than the thinnest wall**, or the reader's half-cell bias
  samples straight through it.
