# RageV — baking sub-roadmap

Written 2026-08-25. A *sub*-roadmap: [ROADMAP.md](ROADMAP.md) still has
untouched items and nothing here displaces them. Companion to
[HANDOFF.md](HANDOFF.md).

This exists because "do we have baked lighting?" turned out to have a
three-part answer, and because the word **baked** is used for three different
guarantees that need keeping apart before any of this is planned.

---

## 0. The three meanings, and which one RageV has

| | what it means | survives a restart | RageV today |
|---|---|---|---|
| **realtime** | recomputed every frame | n/a | SSGI, voxel GI, traced GI, realtime probes |
| **cached** | computed once at runtime, held in memory | **no** | `ProbeUpdate::Cached` (was misnamed `Baked`) |
| **baked** | computed offline, written to disk, shipped | **yes** | **nothing** |

The engine has **no baking of any kind.** Not lightmaps, not irradiance
volumes, not light probes, not SH, not a bake tool, not an on-disk format that
could hold a lighting result. `MeshVertex` is `Position`/`Normal`/`TexCoord`
(`Mesh.h:17-22`) — one UV set — so lightmaps are not merely unimplemented, they
are **unrepresentable** without a vertex-format change.

Every "bake" verb in the codebase is something else: animation curve tables
(`Curve.cpp:104`), colour-grading LUTs (`LutRecipe.cpp:56`), font atlases
(`Font.h:9`), FBX animation flattening (`FbxImporter.cpp:541`).

**Already done (2026-08-25):** the rename to `Cached`, invalidation that
actually fires, and a `Recapture` verb. That was the prerequisite — a bake is
only as good as the ability to say "this is stale".

---

## 1. The five candidates, costed

Effort is in the only units that matter here: how long before it is *checkable*,
not how long to first pixel.

### 1.1 Reflection probes to disk — **S/M, do first**

The machinery all exists: capture, the cosine convolution
(`EnvironmentIBL::IrradianceInto`), the cube arrays, the slot table. What is
missing is only **serialisation**.

- **Needs:** a cubemap asset type, six faces plus mips written and loaded, a
  bake action, and the staleness story (now mostly in place).
- **Size:** ~400–600 lines, no new maths.
- **Buys:** launch no longer re-renders every probe. On a scene with fifteen
  512² probes that is 90 cube faces of scene render before the first frame.
- **Pitfall — format size.** 512² × 6 faces × RGBA16F is ~25 MB *per probe*
  uncompressed. This wants BC6H, which the cooker does not do today. Shipping
  it uncompressed is a non-starter at fifteen probes.
- **Pitfall — the double version bump.** `TextureCook.cpp:17-18` and
  `ImportCache.h:62-86` document that a container format change is two version
  bumps and hard-rejects old paks. This repo has paid for that once.

### 1.2 Irradiance volume, filled at runtime — **M/L, the one that matters**

The piece that serves **dynamic objects**, which is the half of Unity's system
the static flag does not cover. Needs no UV2, no static flag, no vertex-format
change, and can be filled by the path tracer the engine already owns.

- **Needs:** a volume component (bounds, spacing), an SH-L1 or ambient-cube
  payload per cell, per-object or per-fragment lookup, trilinear interpolation
  **with a visibility term**, and a fill pass reusing `TraceSurface`.
- **Size:** ~800–1200 lines.
- **Pitfall — light leaking.** The classic failure: a cell inside a wall gets
  interpolated into a room it cannot see, and light bleeds through geometry.
  Every shipping implementation needs a depth/visibility term (DDGI's chebyshev
  test, or per-cell occlusion). Skipping it is why naive irradiance grids look
  broken. **Budget for this as part of the feature, not a follow-up.**
- **Pitfall — nowhere to put a per-object value.** `InstanceData` is exactly
  256 bytes with a `static_assert`, and every `Indices` lane is now spoken for
  (`x` bone base, `y` probe, `z` material, `w` previous bone base as of the
  motion-vector fix). A per-object lookup needs the struct widened; a
  per-fragment lookup needs the volume reachable from the lit shader, and set 0
  has no free binding (0–17 all used). The scene UBO took the probe table for
  exactly this reason and is the likely home again.

### 1.3 The baker, and the volume on disk — **M, only after 1.2**

A headless tool that loads a scene, builds the same TLAS, runs the same
`TraceSurface` to convergence, and writes the volume.

- **Size:** ~400 lines *given 1.2*.
- **Why after:** the runtime-converged volume is the **oracle**. "Baked and
  runtime agree within tolerance on the same scene" is the whole test, and it
  only exists because 1.2 built the reference first. Doing these in the other
  order means the bake has nothing to be checked against.

### 1.4 Lightmaps — **XL, and I would not**

- **Needs:** UV2 on `MeshVertex` (touching the importer, both cooks, both
  pipelines and every shader), an unwrapper, an atlas packer, an asset type, a
  sampler binding, and a static flag.
- **Pitfall — the unwrapper is the whole cost.** Not the baker. Seams, texel
  density per object, atlas packing, scale changes invalidating the unwrap,
  instanced meshes needing per-instance atlas space. This is the part that
  generates support tickets forever.
- **What it buys:** the GI it replaces costs about **1 ms** (voxel 0.09 ms
  rebuild + 0.9 ms gather; screen-space 0.39 ms). That is the ceiling on the
  win.
- **`ROADMAP.md:140` already recorded the rejection** — "GI | SDFGI, VoxelGI,
  lightmaps | none | **out of scope**". The VoxelGI half is stale (it shipped);
  the lightmap half still holds.

### 1.5 Baked voxel GI — **M, poor trade**

Precompute the voxel cascade and load it instead of revoxelising. Saves ~1 ms,
costs a disk format, a staleness story, and the loss of dynamic occluders (the
voxel form re-rasterises moving geometry every frame, which is exactly what
makes it work for rigid movers). **Rejected: it trades the form's main strength
for its smallest cost.**

---

## 2. The cross-cutting pitfalls

These apply to everything above and are the reason to be conservative.

**No readback in the RHI** (`ENGINE-NOTES.md:1005-1012`). No automated test can
inspect a baked result's *contents*. A mutation that froze every probe on its
first capture passes the entire existing suite. Everything here is guarded on
state transitions and eyeball — which is a structural argument for keeping the
runtime path as the oracle (§1.3) rather than trusting a bake on its own.

**Iteration speed is the real currency.** Today, moving a light changes the
frame. Every baked thing converts some of that into "move a light, wait for a
bake". The engine's own culture is that a frame is a function of the frame
number; the voxel path already violates it and that is logged as a defect
(`ENGINE-NOTES.md:9131-9200`). Baking makes a large part of the frame a
function of a *build step*.

**Static and dynamic must come from one solve.** If lightmaps and probes are
baked separately, or a probe cell sits inside a wall, dynamic objects read a
different answer than the floor they stand on and look pasted on. This is the
failure people attribute to "bad lighting" and it is really a handoff bug.

**There is no static flag.** "Static" in this renderer means *not skinned*
(`Mesh.h:26-29`, `GpuCull.h:29-32`) and nothing else. `GpuCull` rebuilds its
object table with fresh matrices every frame, so nothing downstream currently
cares whether a thing moves — and the awkward case is already in the sample:
the showroom car is rigid but script-moved.

**Probe capture cannot see other probes.** `ProbeSlotFor` returns the sky while
capturing, or two probes facing each other capture each other one frame deeper
every frame. Any multi-bounce bake has to solve this properly (iterate to
convergence) rather than inherit the single-frame rule.

---

## 3. Recommended order

```
done   0.  probe hygiene: Cached rename, invalidation, Recapture verb
       1.  reflection probes to disk           S/M   needs BC6H first
       2.  irradiance volume, runtime-filled   M/L   the one that matters
       3.  the baker + volume on disk          M     2 is its oracle
       never 4. lightmaps                      XL    revisit only if 3 leaves a gap
```

**Do 2 before 1 if only one gets done.** Probes-to-disk is a load-time win;
the volume is the thing that changes how dynamic objects look, which is the
actual complaint. 1 is listed first only because it is smaller and its
prerequisite (BC6H in the cooker) is independently useful.

**A prerequisite that is not on this list:** BC6H compression in the texture
cooker. Both 1 and any HDR bake need it, and without it the on-disk sizes are
indefensible.

---

## 4. What would change this plan

If the pasted-on look turns out to be mostly the **skinned motion-vector bug**
(fixed 2026-08-25, `9b3e53b`) rather than missing bounce, the case for 2 gets
weaker and this whole roadmap can wait. That is worth looking at before
committing to §1.2 — one of the reasons the three defects were fixed first.
