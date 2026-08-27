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

## The order

Ranked by value per hour, not by size. Numbers are engineering judgement.

### 1 · Exponential height fog — ~1 day
One fullscreen pass over the depth buffer. The single largest visual return
available: it is most of what separates a render of a night bridge from a
photograph of one, and nothing else on this list changes an image as much per
hour spent. The engine has **no fog of any kind** — a search of the whole
renderer returns nothing.

### 2 · Reverse-Z depth — ~1 day
**A correctness bug, not a feature.** Depth is conventional `LessOrEqual`
with a conventional projection. At the Golden Gate's scale — a 4 km far plane
against a 5 cm near clip — precision collapses at distance and the far deck
and cables will z-fight. The fix is mechanical: flip the projection, clear to
zero, compare `Greater`. It touches every pipeline's depth state, the shadow
maps, and every pass that reconstructs position from depth (RTAO, SSR, the GI
chain), so it is careful rather than clever.

### 3 · Sky occlusion in the irradiance volume — ~2–3 days
Already briefed in HANDOFF, already chosen by the owner. Store per cell how
much sky it sees; multiply by the live sky colour at shade time. Nearly free
at bake time because the solve already traces the rays and *discards* the
misses. Storage rides in the visibility tile's unused `.y`. **Bump
`BakedLighting::kVersion`** or old bakes read zero and darken everything.

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

### 6 · Volumetric light shafts — ~1–2 weeks
Froxel grid, ray march, temporal reprojection. The effect that puts visible
cones under sodium lamps in sea mist. Do it *after* height fog proves the
appetite — fog delivers most of the mood for a tenth of the work.

### Not scheduled, and why

- **Alpha-cutout materials.** A real gap — every cable and railing must be
  geometry without it — but doing it properly under ray tracing needs
  non-opaque geometry flags and any-hit shading, and modelled cables are more
  accurate anyway.
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
