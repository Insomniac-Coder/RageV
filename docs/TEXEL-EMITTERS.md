# Texel emitters — light through the holes in the black paper

**Status: stages 0 and 1 built and checked, stage 2 planned. 2026-08-24.**
This branch (`texel-emitters`) holds the design and the first two stages;
stage 2 starts only when the owner says so. Nothing on `main` depends on this.

What the measurements changed about the plan, since the plan was written from
reading rather than from running:

- **Stage 1 needs no `MaxLuma` and no change to the membership threshold.**
  The plan feared that folding the mean in would drop a sparse emitter below
  `strength > 1` and leave its glow subtracted-but-unsampled. Keeping the
  threshold on the *scalar*, as it already was, avoids that entirely -- and it
  is also the better answer, because four bright cells are a real light and
  belong in the list where stage 2 can aim at them. Simpler than planned, and
  safer.
- **The split-mesh room is not a usable ground truth for stage 1.** It is
  exact for the estimator, as the research said, but it concentrates the same
  power into four cells while the textured room spreads it over the whole
  ceiling -- and a concentrated source delivers a *different fraction* of its
  power to the floor. Measured 0.82, and no amount of correctness would make
  that 1.00. The check compares against a **uniform ceiling of the same total
  power** instead: same geometry, same distribution, and -- because its scalar
  falls under the threshold -- a different estimator, which is the property
  worth having.
- **Two estimators of one light agree to about 9%** (0.91 measured), which is
  what sets the band in `check_emitters.py`. The failure it guards is 36x.

The owner's idea, in their words: a pixel shader runs per pixel, so a pixel
whose emissive value clears a threshold could be counted as a source of light
— a directional light behind black paper with holes, where light escapes only
through the holes. And the framing that goes with it: lighting becomes two
paths — the traditional ray-traced path with ordinary lights, and a second
path for objects that are *partly* emissive, whose lit portions contribute
light by that rule.

The finding, up front: **the idea is sound, it has an established name —
textured-light importance sampling — and the two-path architecture it implies
already exists in this engine.** Analytic lights already travel their own road
(`CollectLights` → the clustered light buffer → shadow rays); emissive
surfaces already travel another (the area-emitter list → next-event
estimation in the traced bounce). The second road is the one that assumes a
mesh glows uniformly, which is the assumption the showroom defect broke. The
plan below upgrades that road to per-texel and touches the first not at all.

---

## 1. Findings

### 1.1 Where "per-pixel" can live, and why the domain decides everything

"A pixel that emits is a light" must pick which pixels:

- **Screen pixels** (the rendered frame). This is virtual point lights /
  screen-space GI. It is view-dependent by construction: a hole that leaves
  the frame stops existing as a light. RageV **already does this** — SSGI
  gathers from the lit HDR frame (`ssgi_compute.rvshader:50,319-321`), and
  emissive reaches that frame per-texel through the material
  (`pbr_fragment.glsl:1202-1203`), so every bright rendered pixel already IS
  a source to the screen-space form, at full texture resolution. Its limit is
  proven by test: `check_gi.py` claim 4 turns the red wall off screen and
  requires the screen-space form to add *nothing* — "no threshold-fiddling
  can make it pass".
- **Texels** (the emissive texture, in the surface's own domain).
  View-independent: the holes are known ahead of time, from the texture, from
  any camera. This is what offline renderers do for textured area lights
  (per-emitter luminance CDFs / alias tables; Cycles' light tree stores
  texture-aware per-emitter power; Arnold samples textured quad lights by
  texel importance), and it scales down to real time almost unchanged — the
  tables build at load, the shader pays a couple of fetches per sample.
  **This is the correct home for the idea, and the plan below.**
- **Surface cache** (Lumen). Emission is gathered from cached surface
  patches, so partial emissive "just works" — except it is never
  importance-sampled, which is why Epic's own guidance says emissive surfaces
  should be *large and dim* and small bright ones should be real lights. Its
  worst case is exactly the black-paper case, and it is months of
  infrastructure. Not for this engine.

Also surveyed and rejected for now: raw VPLs (superseded by NEE + the traced
bounce), ReSTIR DI (3-6 weeks, adds temporal state that would fight the
existing 0.9 accumulation; worth revisiting only if emitter counts grow past
the flat list — and the texel tables below become its candidate distribution,
so nothing here is wasted), light trees / stochastic lightcuts (overkill at a
16-emitter cap; revisit at hundreds).

### 1.2 What RageV already does with partial emissive, form by form

| GI form | Per-pixel emissive today | Verdict |
|---|---|---|
| Screen-space (SSGI) | Yes — every bright rendered pixel, texture-accurate | Already the owner's idea, but only on screen and within its radius |
| Voxel | Energy only — `voxelize.rvshader:187` samples the emissive map at **LOD 16, its mean**, so a 4-of-144-cells panel voxelises as a uniformly dim panel | Energy-correct smear; would have avoided the phantom but not resolved the holes; floor is the cascade voxel size (0.25-1 m) anyway |
| Traced, hemisphere half | Yes — a hit samples the emissive map at the hit's own UV (`pbr_fragment.glsl:871-873`) | Already texel-accurate |
| Traced, NEE half | **No** — one row per mesh, the material's scalar over the bounds rectangle (`Scene.cpp:1860-1916`) | **The gap. The showroom phantom lived here. This plan replaces it.** |

The asymmetry in the last two rows *is* the defect class: NEE injects
scalar-times-area while hits subtract texture-modulated emissive
(`rtgi_trace.rvshader:315`). Any fix must end with the two halves agreeing by
construction.

### 1.3 Constraints the traced form imposes (all measured in code)

- 4 NEE rays/pixel default, one emitter picked uniformly per ray, one
  visibility ray each (`rtgi_trace.rvshader:167,190,224-232`).
- The NEE residual clamp `kNeeClamp = 32` (`:275-278`) was tuned by a
  measured protocol (`:269-274`) against today's large-dim-rectangle regime —
  it must be re-measured after any change to the sampling distribution.
- `gi_denoise` clips fireflies relative to the brightest 3×3 neighbour
  (`kFireflyScale = 1.25`) — excess variance is *clipped* (energy loss), not
  averaged. High-variance schemes lose energy silently.
- The emissive subtraction at hits fires for **every** hit whenever any
  emitter exists — see the pre-existing defects in §2.0.
- The traced form is Vulkan-only by construction (needs ray queries +
  bindless; `Renderer3D.cpp:895-896`, `RHIDevice.h:207`), so there is no
  OpenGL parity burden anywhere in this plan.

---

## 2. The plan — three stages, each shippable and testable alone

### Stage 0 — make the emitter list honest about its own edges (prerequisite)

Two pre-existing defects, exposed by reading for this design, and the later
stages are unsafe without the fix:

1. **Over-cap emitters lose their light.** The docs promise hemisphere
   fallback past the 16 cap (`Renderer3D.h:32-35`, `rtgi_trace.rvshader:94-98`)
   but `SetAreaEmitters` truncates with `break` (`Renderer3D.cpp:1243-1244`)
   while the shader still subtracts emissive at every hit — emitters 17+ are
   subtracted-but-never-sampled. Their light vanishes today.
2. **Subtraction is global, membership is filtered.** Sub-threshold glows
   (`strength > 1` gate) and degenerate-dropped rectangles are likewise
   subtracted-but-unsampled whenever any emitter exists.

Fix: a `RAY_INSTANCE_EMITTER` flag bit on the ray-instance row (`_pad0` free
at `pbr_fragment.glsl:206-208`; Scene builds the rows at `Scene.cpp:2436` and
knows which meshes joined `m_Emitters`), carried through `TracedSurface`, so
the subtraction at `:315` fires only for meshes NEE actually answers for.
~15-20 lines. Note while wiring it: **blended meshes never enter the ray
structure** (`Scene.cpp:2433-2434`) but the emitter walk has no blended gate —
a blended emissive mesh is NEE-only today (nothing to subtract, so the flag
design must simply tolerate emitters with no ray instance).

*Test:* a fixture with 18 emitters must render the 17th and 18th's light
(today it goes dark); a sub-threshold glow must not dim its own surroundings.
*Falsify:* revert the flag to the global subtraction, watch it fail.

### Stage 1 — mean-scaled emitters (the energy fix, everywhere, ~150-180 lines)

`Radiance = resolved scalar × mean(emissive map)`. The phantom becomes
impossible in any scene, authored however the artist likes; a masked texture
contributes its true power, spread (still) over the rectangle.

- **Stats at load time, not cook time.** Decode the smallest cooked mip
  ≤ 32² on the CPU (~40 lines of BC1/BC3 block decode), linearize, store
  `MeanRgb` + `MaxLuma` in a loader-side registry keyed on the texture.
  Deliberately NOT stored in the cooked RVTX container: that would be format
  v2 and the documented double version bump (`TextureCook.cpp:17-18`,
  `ImportCache.h:62-86` — the trap this repo has already paid for once), plus
  hard rejection of old paks. Load-time derivation has zero disk-format
  surface. Cook-time storage stays available as a later optimisation only.
- **Membership switches to `scalar peak × map max-luminance > 1`** — the
  mean must NOT drive the threshold: emissive maps are LDR, so mean-folding
  only ever reduces radiance, and a sparse panel (mean ≈ 0.03) would fall out
  of the list while its glow kept being subtracted at hits — the original
  defect inverted. (Safe only after Stage 0.)
- Per-entity emissive **overrides keep working unchanged**: the walk already
  resolves them (`Scene.cpp:1883-1884`); map stats are per-texture and
  override-independent; radiance stays `resolved scalar × stats`.
- **Invariant that keeps the test suite green:** a material with no emissive
  map gets `mean = max = 1` — scalar-only fixtures (all of check_gi's) are
  byte-stable through this change.

*Test:* a `make_holes_scene.py` fixture — a dark room, one plane whose
emissive texture is black with N bright holes, beside the same wall built as
N separate small emissive quads (the split-mesh construction the showroom now
uses — it is the in-engine ground truth, and it is exact for NEE by the
"flattest rectangle of a plane" contract). After Stage 1 the textured plane's
*total* bounce energy must land within a band of the split-mesh room's;
before it, it reads ~N× too bright. Also assert the second in-engine ground
truth: `emitters == 0` (hemisphere-only) at high ray count converges to the
same mean — the GpuLit lesson says never trust a single reference that might
share the bug.
*Falsify:* drop the mean factor (radiance back to the raw scalar) — the
energy band must fail loudly.

### Stage 2 — texel importance sampling (the holes themselves, ~450-580 lines)

The black paper with holes, done in the texture domain. Per emissive map, a
32×32 luminance grid with an alias table, built beside the Stage-1 stats.
NEE picks a cell ∝ its luminance, jitters inside it, maps cell-UV → rectangle
→ world through an affine map, traces the visibility ray to that point, and
evaluates the **actual texture** there. Samples land only on the holes;
radiance at each is the true texel value; the estimator's two halves agree by
construction because both sample the same map at lod 0.

The load-bearing details, all verified against code during research:

- **UV ↔ rectangle affinity must be computed, not assumed** — the constants
  differ even between the engine's own Plane and Quad primitives (verified
  from `Mesh.cpp:276-318` against the AABB walk's axis order). Derive the six
  affine floats from three vertices' (position, uv) pairs; validate on the
  rest; any mesh that fails (non-planar, non-affine UVs) falls back
  per-emitter to Stage-1 behaviour (`CdfSize = 0`). Mesh deliberately does
  not retain UVs (`Mesh.h:136-139`) — retain them for small meshes only
  (≤ ~64 vertices; a light fitting is 4).
- **Tiling**: integer `UvTransform` tiling stays exact (pick a tile
  uniformly; the probabilities cancel); fractional tiling breaks the
  bijection → fallback, logged once per material.
- **PDF**: `pdf_A = p(cell) · Gw·Gh / area`; the affine Jacobian is constant,
  so non-uniform entity scale cancels identically (both the sample point and
  `area = 4|U×V|` come from the same world tangents). Today's shader line is
  the special case `p·G = 1, L = scalar`.
- **No cap pressure**: still one row per mesh — the CDF handles the inside of
  the rectangle. This is the decisive advantage over "one mesh per lit
  region" (the showroom's authoring workaround, which spends emitter slots
  and artist time per region).
- **Plumbing**: `GpuEmitter` grows two vec4s (affine + CdfOffset/Size/heap
  slot/tiling) — the CPU/GPU struct mirror gets a `static_assert`, because
  dual-definition drift is a documented silent-bug class here. CDF buffer at
  set 3 binding 3 (free; set 0 is untouchable, set 2 is the heap), uploaded
  beside `GiEmitters` with the same at-least-one-row pattern. ~8 KB per
  emitter, ≤ 128 KB.
- **Copy-not-reference discipline**: `m_Emitters` is copied per frame because
  probe captures re-enter the render mid-frame (`Renderer3D.h:38-41`, 7bw) —
  the new per-row `shared_ptr` stats must ride that copy, never point back
  into scene state.
- **Baked probes hold the old energy** until re-captured — same
  probe-poisoning failure mode as the GpuLit defect. The showroom's
  mode-switch re-bake covers it there; the plan's test fixture uses no baked
  probe, and the doc for the change must say "re-bake probes after upgrading".
- `kNeeClamp` gets its measured protocol re-run (expect it to trigger
  *less* — value/pdf is far flatter than today's phantom regime). The RNG
  sequence shifts by one draw per sample, so golden-image comparisons see a
  different — not worse — noise pattern.

*Test:* the same holes fixture, now asserting the *spatial* claim: the wall
opposite each hole must show its pool of light where the split-mesh ground
truth shows it (region-wise band comparison, both backends' traced form —
i.e. Vulkan only, by construction), plus the temporal-crawl assertion that
caught the showroom (consecutive settled frames within a few levels — the
phantom read 92). And the showroom itself becomes an end-to-end A/B: a
4-lit-cells *textured* panel must render within a band of the shipped
split-mesh version.
*Falsify:* three independent breaks — drop `p(cell)` from the pdf (energy
skews toward large dark cells), sample lod 4 instead of 0 (halves disagree,
the subtraction consistency fails), swap the affine u/v (holes light the
wrong walls).

### Deliberately not in the plan

ReSTIR (revisit if emitters outgrow the flat list — Stage 2's tables become
its candidate distribution), light trees (revisit at hundreds of emitters),
surface cache (wrong scale, and its worst case is this exact case), and any
change to the analytic-light path — the owner's "traditional" path is
untouched at every stage.

---

## 3. Order, size, risk

| Stage | Size | Risk lives in | Ships alone? |
|---|---|---|---|
| 0 — subtraction flag + cap contract | **built**: ~90 lines over 7 files | the flag's blended/no-ray-instance edge | Yes — it fixed two real defects |
| 1 — mean-scaled emitters | **built**: ~200 lines over 6 files, no shader/format change | BC decode correctness | Yes — kills the phantom class engine-wide |
| 2 — texel alias sampling | ~450-580 lines, 6-7 files | struct mirror; pdf; affinity gate | Yes — needs 1's stats machinery |

Stages 0 and 1 are guarded by `tools/scripts/check_emitters.py` over the
fixtures `make_emitter_scene.py` writes, and both claims were watched to fail:
reverting stage 1's fold takes the textured room to 3.33x the reference (36x in
radiance, on a floor that saturates), and reverting stage 0's flag takes the
glow room to **exactly black** -- a sealed emitter in a corner deleting every
photon in the room, which is the defect in its purest form.

Recommended: land 0+1 together (1 is unsafe without 0), then 2. Every stage
carries its check script and its falsification, per CHK.2: a claim nobody has
seen fail is a claim nobody has read.
