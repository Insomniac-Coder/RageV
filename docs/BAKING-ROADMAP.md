# RageV — baking sub-roadmap

Written 2026-08-25. A *sub*-roadmap: [ROADMAP.md](ROADMAP.md) still has
untouched items and nothing here displaces them. Companion to
[HANDOFF.md](HANDOFF.md).

This exists because "do we have baked lighting?" turned out to have a
three-part answer, and because the word **baked** is used for three different
guarantees that need keeping apart before any of this is planned.

---

## 0. The three meanings, and which one RageV has

> **This section describes 2026-08-25, before any of it was built.** It is kept
> for the definitions, which are still the ones this document uses. The "RageV
> today" column and the paragraph under it are **no longer true** — irradiance
> fields and probe cubes are both baked to disk now (§1.1–§1.3, all shipped).
> Read §3 for where the work actually stands. *(Corrected 2026-09-01, after an
> audit found this page still telling readers the engine cannot bake.)*

| | what it means | survives a restart | RageV in 2026-08-25 |
|---|---|---|---|
| **realtime** | recomputed every frame | n/a | SSGI, voxel GI, traced GI, realtime probes |
| **cached** | computed once at runtime, held in memory | **no** | `ProbeUpdate::Cached` (was misnamed `Baked`) |
| **baked** | computed offline, written to disk, shipped | **yes** | **nothing** |

At that date the engine had **no baking of any kind.** Not lightmaps, not
irradiance volumes, not light probes, not SH, not a bake tool, not an on-disk
format that could hold a lighting result. `MeshVertex` is
`Position`/`Normal`/`TexCoord` (`Mesh.h:17-22`) — one UV set — so lightmaps are
not merely unimplemented, they are **unrepresentable** without a vertex-format
change. **That last sentence is the one thing here still true today**; it is why
§1.4 stays refused.

Every "bake" verb in the codebase is something else: animation curve tables
(`Curve.cpp:104`), colour-grading LUTs (`LutRecipe.cpp:56`), font atlases
(`Font.h:9`), FBX animation flattening (`FbxImporter.cpp:541`).

**Already done (2026-08-25):** the rename to `Cached`, invalidation that
actually fires, and a `Recapture` verb. That was the prerequisite — a bake is
only as good as the ability to say "this is stale".

**Done (2026-08-26): §1.2 and §1.3.** Fields and probes are written beside their
scene and read back on later runs, keyed per lighting, composed from however
many volumes a scene has, and selectable through a Realtime/Baked source on both
GI paths. The showroom runs on it: 3.73 ms against 7.4, 267 fps against 134.
What baking does *not* yet cover is the light that moves — see HANDOFF's opening
section, which leads on it. The account below is how §1.2 got there.

**In progress (2026-08-25):** §1.2, the irradiance volume. The field is plumbed
end to end and reads correctly (`30e15bb`), the solve's shader is written and
its projection verified (`7194a9b`), and **the pass that runs it now exists** — a
third render-graph pass kind, `Standalone`, because the fill has to run after the
scene pass, outside a render pass, and record its own barriers. A field is solved
once and held.

**And it is worth what it claimed to be**, which is the part that had to be
measured rather than asserted: one traced bounce plus the field lands at 0.735
mean levels of error against a true two-bounce render, where one bounce alone
is 1.483 — half the quality of a second bounce for a seventh of its per-frame
cost, and the solve happens once. Getting there meant fixing a currency
mismatch that had the field counting the first bounce and the sky twice over,
and it meant giving traced hits a shadow ray for local lights, without which a
field solved in a lamp-lit room bakes in light through walls.

**§1.2 is now built out.** The visibility term this file said to budget for is
in: every solve writes an octahedral map of sixty-four directions per cell --
how far geometry is that way -- from the same rays that carry the light, with no
flag to turn it off, and the lookup weights each cell by Chebyshev's test
against it. The solve is amortised over frames and sweeps four times to average
its noise; rotation on a volume's transform is honoured rather than ignored.
Current: **0.720 against the two-bounce reference for +0.055 ms**, with a lamp
behind a wall reading exactly black.

What it does *not* have is probe relocation, and that is the one leak left: at
cell spacings coarser than a wall is thick, a sealed room beside a lit one still
reads 3.4 levels of 255 (0.000 at the default one-metre spacing). Four
representations of stored distance were measured against that case and none
closed it; the rule for authors is that cells want to be smaller than the
thinnest wall they straddle. Nested volumes remain scoped out here.
[HANDOFF.md](HANDOFF.md) opens with all of it, the numbers, and the testing rule
that a run without a volume in the scene proves nothing.

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

### ~~1.2 Irradiance volume, filled at runtime~~ — ✅ **done 2026-08-26**

*The component is `IrradianceVolumeComponent`, the runtime field is a live
`Scene` member solved every frame, and the fragment reads it as
`u_IrradianceField` at set 0 binding 18. Chebyshev visibility went in with it,
as the "budget for this as part of the feature" note below insisted. The plan
below is kept for its reasoning.*

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

### ~~1.3 The baker, and the volume on disk~~ — ✅ **done 2026-08-26**

*The baker is a child `RageVRuntime.exe` launched with `--bake=force`, driven
from the editor's Bake button. Fields are written beside the scene as
`.rvfield`, in an rt/ss pair, adopted only on a matching stamp. The plan below
is kept for its reasoning.*

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

> **No longer true (2026-09-03).** `MeshComponent::Static` and
> `TerrainComponent::Static` exist, default off; the bake sees static objects
> only, static surfaces read fully baked lights from the field, moving ones
> take them live and still shadow the static floor through a ray traced
> against the moving objects alone. ENGINE-NOTES 7cx. The car is the awkward
> case no more: it is simply not static.

**Probe capture cannot see other probes.** `ProbeSlotFor` returns the sky while
capturing, or two probes facing each other capture each other one frame deeper
every frame. Any multi-bounce bake has to solve this properly (iterate to
convergence) rather than inherit the single-frame rule.

---

## 3. Recommended order

```
done   0.  probe hygiene: Cached rename, invalidation, Recapture verb
done   1.  reflection probes to disk           S/M   needs BC6H first
done   2.  irradiance volume, runtime-filled   M/L   the one that matters
done   3.  the baker + volume on disk          M     2 is its oracle
       never 4. lightmaps                      XL    revisit only if 3 leaves a gap
```

**This order is spent** — 1, 2 and 3 all shipped between 2026-08-26 and
2026-08-28, and the paragraph that used to stand here ("do 2 before 1 if only
one gets done") was advice for choosing between two jobs that are both
finished. What 1 did *not* deliver is recorded in §1.1; it is a remainder, not
a reason to do 1 again.

**A prerequisite that is not on this list, and is still not built:** BC6H
compression in the texture **cooker**. Both 1 and any HDR bake need it, and
without it the on-disk sizes are indefensible.

*Not to be confused with the BC6H that does exist.* `BakedLighting.cpp`
carries its own encoder, and it is why a 512-pixel probe cube is 1.5 MB on
disk rather than 12.5. That one is private to the `.rvprobe` container.
`TextureCook` — the path every imported image takes — still offers only
RGBA8/BC1/BC3/BC4/BC5 and accepts 8-bit input alone, so it cannot carry an HDR
source at all. An audit on 2026-09-01 read the first and concluded this line
was stale; it is not.

---

## 4. The caveat, checked -- and it does not hold

Before committing to §1.2 it was worth asking whether the pasted-on look was
mostly the **skinned motion-vector bug** (fixed 2026-08-25, `9b3e53b`) rather
than missing bounce. If it were, this roadmap could wait. It is not.

Measured on `ray_shadows_local` by differencing a GI-on render against a GI-off
one -- which isolates the indirect light the character actually receives -- and
comparing that with the fix in and out, at two resolutions and two frames:

| | |
|---|---|
| pixels the fix changes | **0.22% of the frame**, max 58-81 levels |
| the fox occupies | ~10% of the frame |
| so, of the character's own pixels | **~2% changed** |
| indirect as a share of the fox's shading | **8.7%** |

The bug was real and worth fixing -- it corrupted those pixels badly, and they
sit where the pose deviates most from bind and where the indirect gradient is
steepest, which is the silhouette. But "pasted on" is a *whole object* reading
wrong against its surroundings, and a defect touching two per cent of a
character cannot produce that.

What remains is structural and is what §1.2 exists for: a dynamic object's
indirect light is one probe cube chosen for the whole object plus a
screen-space buffer, with nothing that varies across the object and nothing
that answers "what is the irradiance at this point in space".

Two limits on the number. 8.7% is this scene -- a small test room with a strong
direct spot; where bounce carries more of the lighting the share rises and so
does the payoff. And one run out of five reported zero difference and could not
be reproduced; the other four agree closely, so it is recorded as an artifact
of that run rather than evidence.
