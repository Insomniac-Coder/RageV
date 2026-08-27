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

### 0 · Alpha-cutout materials — ~2–4 days *(owner-set, next)*
No alpha-tested mode exists; glTF `MASK` is read as `BLEND` with a soft edge.
Every railing, grate and cable must therefore be geometry, which is the
Golden Gate scene's largest avoidable cost and compounds the missing LODs.
**The raster half is easy and gives OpenGL the whole feature; the ray-traced
half must go inside the ray-query traversal loop, because this engine has no
any-hit stage to write.** Full brief in HANDOFF.

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
