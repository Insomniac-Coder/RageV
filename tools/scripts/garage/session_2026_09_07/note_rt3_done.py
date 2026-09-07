"""RT-3's record into docs/RT-SERIES.md, and its row marked done."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, '### RT-3 —'):
    print('RT-SERIES.md already has RT-3\'s record')
    raise SystemExit(0)

# the row in the build-order table
s = rep(s,
    "| One contract for every signal, no second temporal system fighting it (WR-16's ReSTIR-GI rejection stands). | medium |",
    "| One contract for every signal, no second temporal system fighting it (WR-16's ReSTIR-GI rejection stands). | medium — **✅ done 2026-09-07, record below** |")

RECORD = """
### RT-3 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**What it does.** Under ray tracing the bounce is now a signal of *this* frame:
`rtgi_trace` runs between the G-buffer pass and the lit pass (it already read
only depth and normal, so it needed no new input), a joint bilateral upsample
brings it to the lit pass's grid, the reconstruction contract settles it, and
the lit shader fetches it by texel at binding 16 — the same binding the
one-frame-late buffer used, with `RayRates.w` bit 24 saying which of the two is
in it. `gi_denoise` and the one frame of latency it carried are gone from the
traced path. `--gi-signal=off` is the reference arm and puts the traced form
back on the old chain whole; the screen-space forms (SSGI, voxel) stay there
always, because their gather reads the lit image and cannot run before the pass
that makes it. New: `gi_upsample.rvshader`, `PostProcess::GiUpsample`,
`Renderer3D::GiSignal()`/`SetGiSignal`/`SetScreenIndirectSignal`,
`FrameDesc::GiLight` (its own history in both layers), `--gi-signal=on|off`,
`--debug-view=gi-light` and `gi-refusal`.

**The audit RT-3 asked for: `gi_denoise` against the contract's four properties.**

| property | `gi_denoise` | the contract | verdict |
|---|---|---|---|
| reprojection **by surface** | by the velocity buffer alone, then a 3x3 colour clamp. Its own header says so: *"a clamp only rejects history where the neighbourhood varies, and indirect light is nearly uniform almost everywhere — which is exactly where a wrong reprojection hides"*, and *"the off-screen rejection is"* what makes it correct. On a near-uniform signal that is the whole of its geometry. | rebuilds the surface at the texel centre, reprojects through the previous projection minus the previous jitter, and refuses a history whose normal (dot < 0.8), plane (0.05 + 0.01·eye) or roughness (0.5) disagrees — then searches the eight neighbours (SVGF) before giving up. | **contract wins outright.** This is the property `gi_denoise` never had and could not get without a G-buffer. |
| **geometric tests** | none. Disocclusion is caught only by the reprojection landing off screen, and by whatever the weak colour clamp catches. | the three above, plus the silhouette rule and the id lane. | **contract.** |
| **motion-capped memory** | `kMaxFrames = 16` and a fixed feedback, both constants; nothing measures how far the reprojection moved. A texel sliding a pixel a frame still averages sixteen positions. | memory halved per texel of travel, capped so the average never spans more than `SmearTexels` of it, floored at `MovingMemory`, and shortened again at a silhouette whose other side moves. | **contract.** |
| **a bound** | a good one, and the one place `gi_denoise` is ahead: it accumulates in a range-compressed space (`c/(1+luma)`), bounds the fresh centre at 1.25× the brightest neighbour before the box is built from it, widens the box by the neighbourhood's *spread* rather than its extremes, and floors it at the pixel's own temporal sigma. | the same shape — mean and spread of the fresh 3x3, floored at the pixel's measured fluctuation — but **no range compression and no firefly bound on the fresh sample**. | **`gi_denoise` ahead on two counts.** Kept as an open item below rather than merged blind: the contract's bound was tuned against three other signals and the compression changes what every one of them averages. |

So: replaced, and the one thing the old denoiser did better is written down rather
than lost. Its spatial 3x3 blend (`kSpatial = 0.65`) is answered by the contract's
three a-trous blurs at strides 1, 2, 4 with `YoungRadius = 12`, the widest young
blur any signal here takes.

**The one contract change: `BoundWidth` 3 → 6 for this signal.** The bound is
built from the fresh 3x3's spread, which assumes the neighbours are independent
estimates. After an upsample from half resolution they are not — four
full-resolution texels share one traced sample — so the measured spread
understates the real variance and a bound built from it refuses a history that
was never wrong. The temporal floor is what holds this signal; this keeps the
spatial ceiling out of its way. One number in `GiSignal()`, no shared code touched.

**Why upsample first and accumulate at full resolution** (the complexity note's
open question: *"the contract assumes the signal is at the G-buffer's resolution
and GI traces at half; the accumulate's texel-to-surface lookup needs a scale"*).
It is RT-2's answer, and RT-2 is the precedent that settles it: a
half-resolution texel sits on the corner of four full-resolution ones, so
"the surface under this texel" — the question every one of the contract's tests
asks — has four answers there, and a sampled depth across a silhouette is the
depth of neither surface. Upsampling first makes the tests honest and needed no
change to code four signals share. **It is also the expensive choice, and the
price is measured below.**

**The picture: no regression, and no visible win either.** Both scenes, converged
still, the signal against `--gi-signal=off`:

| scene | mean \\|d\\| | p99 | max | pixels > 2 levels | frame signed |
|---|---|---|---|---|---|
| garage (forced `--gi-source=realtime`) | 0.098 | 1.33 | 42.3 | 0.31% | +0.046 |
| camp (realtime GI, the series' named test) | 0.079 | 0.67 | 30.3 | 0.09% | +0.048 |

The diff images (`build/rt3/garage_off_vs_on_x8.png`, `camp_off_vs_on_x8.png`) are
near-black at ×8 with a faint uniform green on walls and pillars — the a-trous
blur against `gi_denoise`'s 3x3, slightly favouring the signal — and no structure,
no halo at any silhouette, which is the joint bilateral upsample doing its job.
Region by region on the garage the bounce is preserved to within 0.03 levels.

**The reference arm is bit-identical to the pre-RT-3 build** (mean 0.0000, max
0.0, on the garage's parked frame 169): `--gi-signal=off` is a true A/B, not the
same storage in a different shape.

**The lag it removes is not measurable in either scene, and the reason is worth
recording.** The one-frame-late buffer's error is a camera-motion error, so it
should show under the dolly and not when parked. It does not: the two arms differ
by 0.104 levels at frame 40 and 0.114 at frame 189, flat across the whole move.
The direct test — is the reference at frame N closer to the signal at N or at
N−1? — answers **N** at every frame by a factor of forty (0.12 against 5.5),
because a frame of camera travel moves the picture by 5.5 levels and **the entire
traced bounce is worth +0.383 levels in the garage and +0.588 in the camp**. A
one-frame error in a term that small cannot be seen. RT-3's win here is
architectural: one contract, one temporal system, and the trace reading the
G-buffer like everything else. A scene where the bounce carries real light would
be needed to show it in a picture, and neither test scene is one.

**The cost, and it is the honest problem with this item.**

| | garage (realtime GI) | camp |
|---|---|---|
| frame, `--gi-signal=off` | 15.04 ms | 6.21 ms |
| frame, `--gi-signal=on` | **16.62 ms** | **6.93 ms** |
| the old chain (trace + `gi_denoise`) | 2.796 + 0.081 | 1.091 + 0.080 |
| the new chain (trace + upsample + accumulate + three blurs) | 3.046 + 0.041 + 0.223 + 0.252 + 0.205 + 0.170 | 1.111 + 0.042 + 0.207 + 0.188 + 0.168 + 0.113 |
| the GI passes' delta | +1.06 ms | +0.66 ms |

**Where it goes: the contract runs at full resolution on a half-resolution
signal, so its four passes pay four times the texels they carry information for**
— 0.85 ms of the garage's 0.89 ms chain is the accumulate and the three blurs.
`gi_denoise` ran at half. Halving the contract's resolution would put those four
passes at roughly 0.22 ms and save about 0.63 ms, but it needs the contract
taught a scale for its G-buffer lookups — the shared code the upsample-first
choice was made to avoid. **That is a decision for the owner, not a default: it
is the same trade RT-2's occlusion signal made silently, and it is now priced.**

**Scenes that do not use realtime GI are untouched, verified rather than assumed:**
the garage at its own baked setting and the bridge at Headland build zero GI
signal passes, and the bridge's Headland still against RT-2.1's accepted capture
is mean 0.034 levels, p99 0.67, no structure.

**Three defects found on the way, all in this item's own plumbing, and the
instrument that found them.** The first two were invisible to any comparison of
finished frames, which is the lesson:

1. **The signal was computed every frame and read by nobody.** `u_Scene.Indirect.x`
   is both the GI intensity and the switch that says there is a bounce to add, and
   it was filled only when the one-frame-late history had a frame in it — right for
   a buffer written last frame, wrong for a signal computed for this one, which the
   signal path never advances. **A probe writing a bright constant into every texel
   of the upsample moved the frame by exactly as much as the real signal did (mean
   0.799 levels, max 53.0, to three decimals identical), because neither was read.**
   That equality is the tell, and nothing else in the measurement kit would have
   shown it: the arms differed, the difference had a plausible size, and it was
   entirely the *old* chain being switched off. After the fix the same probe moves
   the frame 274× more than the signal does.
2. **The two paths into binding 16 disagree about what the alpha means.**
   `gi_denoise` writes 1 — a validity flag the lit shader multiplies into the
   bounce; the contract writes the *frame count* (`o_Accumulated = vec4(kept,
   frames)`), and zero where no surface stood. The lit shader was multiplying the
   bounce by a number that runs to 64. The same class of unit error 7ay recorded
   when linear depth arrived in this channel and read as a feedback loop. Under the
   signal the irradiance is taken whole and the count is read as what it is: one or
   more means this pixel has an estimate, zero means the field answers.
3. **A null texture into `SetTexture`** — segfault, not a validation message. Once
   `haveIndirect` could be true with no texture behind it (which is the point: the
   picture arrives in `DrawLit`, after `BeginScene`), both binding-16 sites had to
   fall back to transparent black rather than pass the null through.

**Open, filed here rather than fixed:** (a) the contract's bound has no range
compression and no firefly bound on the fresh sample, which `gi_denoise` had and
which a hemisphere estimate wants — it belongs to RT-5, where the bound is the
subject, and it changes what all four signals average; (b) the full-resolution
contract on a half-resolution signal, priced above at ~0.63 ms; (c) the traced
bounce is worth under 0.6 levels in both test scenes, which is a question about
the scenes or the GI settings rather than about RT-3, and is the reason this item
has no picture to show for itself.
"""

s = rep(s,
    "\n## The S series, for the record (owner asked 2026-09-06)\n",
    RECORD + "\n## The S series, for the record (owner asked 2026-09-06)\n")
write(D, s)
print('RT-SERIES.md: RT-3 record written')
