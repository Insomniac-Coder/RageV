"""RT-3.1's record into docs/RT-SERIES.md, and a row for it."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, '### RT-3.1 —'):
    print('RT-SERIES.md already has RT-3.1')
    raise SystemExit(0)

ROW = ("| **RT-3.1** | **The contract runs at each signal's own resolution.** "
       "RT-2 and RT-3 both upsampled a half-resolution signal to the frame and then ran the whole "
       "reconstruction contract there, paying four times the texels a half-resolution estimate carries "
       "information for. Instead: a guidance downsample (the G-buffer's depth, normal and velocity lanes "
       "onto the signal's grid, by selection), the contract at that grid, and one joint bilateral upsample "
       "at the end -- the arrangement every real-time denoiser uses. Both the occlusion and the bounce. "
       "| owner-directed 2026-09-07, from RT-3's cost | The filter belongs where the signal was traced; "
       "upsampling first was the smaller diff, not the better design. | small -- "
       "**✅ done 2026-09-07, record below** |\n")

s = rep(s,
    "| **RT-4** | **Reflections as an instance of the shared code, traced from the G-buffer.**",
    ROW + "| **RT-4** | **Reflections as an instance of the shared code, traced from the G-buffer.**")

RECORD = """
### RT-3.1 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**Owner-directed, and the reason is worth writing down.** RT-2 and RT-3 both
traced at half resolution, upsampled to the frame, and ran the contract there.
The argument for it was real -- the contract validates history by the surface
under each texel and reads depth, normal and velocity with
`texelFetch(..., ivec2(gl_FragCoord.xy))`, which is only honest on the
G-buffer's own grid -- but the conclusion was the smaller diff, not the better
design: **the standard arrangement is to filter at the resolution the signal was
traced at and spend one cheap bilateral upsample at the end**, and what that
needs is guidance lanes on the coarse grid, which is a new pass rather than a
change to shared code. The owner called it: *"you SHOULD NOT take the easy
approach, now rework the AO and GI task and make them use half resolutions."*

**What it does.** A `Guidance downsample` pass writes the G-buffer's depth (32-bit,
it feeds an inverse view-projection), normal lane and velocity onto a signal's
grid, **each one a whole texel, never averaged** -- the average of two normals
across a silhouette faces neither way, the average of two depths is a plane
in front of one surface and behind the other, and the average of two velocities
is the motion of nothing. `addSignal` takes a `SignalGuidance` (the three lanes
and a divisor); passing none means the G-buffer and a divisor of one, which is
what the reflections and the direct light pass, so **they are untouched by
construction**. Built once per distinct divisor and shared, so GI and the
occlusion on the same rung pay for one downsample between them.

**Which texel the downsample picks, and the alternative that was measured.** The
one the contract itself would have read at full resolution: `t * d + d/2`, in
integer texels through `gl_FragCoord`, so no uv convention or row flip can come
between the two sides. **Nearest-to-camera -- the conventional rule -- was tried
and is worse:** on the bridge it took the error against the full-resolution
contract from 0.190 levels to 0.299 and p99 from 3.00 to 5.33. Matching the
grid the contract reasons on beats picking the most prominent surface in the
footprint.

**The tuning follows the grid.** `Slack`, `SmearTexels`, `YoungRadius` and
`MaxRadius` are all counted in texels, and a half-resolution texel covers twice
the screen; they are scaled by the divisor inside `addSignal`. Left alone, a
signal moved down would have held its history through twice the camera motion
before the smear cap bit, and blurred half as far across the picture.

**The occlusion needed one extra thing.** Its raw buffer is
`vec4(ao, linearDepth, 0, 0)` -- the compute pass carries its own depth in G for
its bilateral tap weights -- and that payload cannot enter the contract, whose
bound and moments are built from `Luma(rgb)`: with metres in the green channel
the luma is almost entirely the depth. So the resolve that already strips it
(white scene, intensity folded in) moved *down* to the occlusion's own
resolution, where its bilateral weights collapse to a passthrough. That also
keeps RT-2's order -- the intensity curve applied before accumulation, as it was
when the owner accepted the look.

**The cost, which is the point of the item.**

| chain (garage, realtime GI) | RT-3 | RT-3.1 | saved |
|---|---|---|---|
| GI: upsample + accumulate + three blurs | 0.891 ms | 0.268 ms | **0.62** |
| occlusion: resolve + accumulate + three blurs | 0.732 ms | 0.258 ms | **0.47** |
| the guidance downsample (shared) | — | 0.018 ms | −0.02 |

| frame | RT-3 | RT-3.1 |
|---|---|---|
| garage, `--gi-signal=on` | 16.62 ms | **14.97 ms** |
| garage, `--gi-signal=off` (the occlusion alone moved) | 15.04 ms | **14.79 ms** |
| garage at its own baked setting (occlusion only) | 11.63 ms | **11.39 ms** |
| camp, on / off | 6.93 / 6.21 ms | **5.92 / 5.67 ms** |

**The GI signal now costs +0.18 ms over the one-frame-late chain it replaced**
(garage), against +1.59 ms when RT-3 landed. Every scene with an occlusion
signal gets the 0.24 ms whether or not it traces GI.

**The picture: small everywhere, except on thin geometry.**

| comparison | mean \\|d\\| | p99 | max | > 2 levels |
|---|---|---|---|---|
| garage, occlusion alone, full-res contract → half | 0.081 | 2.00 | 63.0 | 0.95% |
| garage, both signals | 0.084 | 2.00 | 62.7 | 0.94% |
| camp, both signals | 0.119 | 2.00 | 48.3 | 0.85% |
| **bridge (Headland), occlusion alone** | **0.190** | **3.00** | **178** | **1.20%** |

The garage and camp diffs are thin lines on beam, pillar and car edges. **The
bridge is the one that matters and it is a real loss:** the cables and vertical
suspenders come out darker -- signed −0.577 levels over the cable band, worst
pixel 207 -- while the water and sky are unchanged (+0.029). One-to-two-pixel
geometry is where half-resolution filtering loses, because a coarse texel's
guidance describes whichever surface the selection picked and a cable often is
not it; the full-resolution contract could keep a per-pixel history for the
cable and this cannot. **Owner's word (2026-09-07): the bridge cables are a
known problem with a flicker of their own, to be dealt with separately** -- so
this is filed as evidence for that work, not as a blocker here. It is a
specific, reproducible symptom of the same thin-geometry weakness.

**A normal term in the upsample was tried and measured out.** Weighting each tap
by how nearly it faces the way the pixel does, on the theory that the residual
edge lines were the upsample spreading one surface over its neighbours: 0.081 →
0.079 levels, p99 unchanged at 2.00, share of pixels off by more than two from
0.95% to 0.90% -- for 0.28 ms. The residual is the contract validating on the
coarser grid, not the upsample choosing badly, and those edges are edges in
*depth*, which the existing term already sees. Removed; the reasoning is kept in
`signal_upsample.rvshader`'s header because it is an obvious idea to have twice.

**Shape now, both signals:** trace (or compute) at the dial's resolution →
[occlusion only: resolve, stripping depth and folding in the intensity] →
`Guidance downsample` → accumulate → three a-trous blurs → `SignalUpsample` to
the lit pass's grid → the lit shader reads by texel. `gi_upsample.rvshader`
became `signal_upsample.rvshader` and `PostProcess::GiUpsample` became
`SignalUpsample`, since two signals share it now. `PostProcess::Dispatch` grew a
third colour attachment for the guidance pass's three lanes.

**Verified unchanged:** the reflections and the direct light (divisor one, the
G-buffer's own lanes -- the same arguments they passed before); the garage at
its own baked setting; raster mode (`--ray-tracing=off`, 11.80 ms, clean).
"""

s = rep(s,
    "\n## The S series, for the record (owner asked 2026-09-06)\n",
    RECORD + "\n## The S series, for the record (owner asked 2026-09-06)\n")
write(D, s)
print('RT-SERIES.md: RT-3.1 record written')
