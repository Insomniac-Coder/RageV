"""RT-1 done: the record and the S-series status into docs/RT-SERIES.md, the
hand-off header, memory."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new):
    o = old.replace('\n', nl); assert s.count(o) == 1, (s.count(o), old[:70]); return s.replace(o, new.replace('\n', nl))

p = 'docs/RT-SERIES.md'; s, nl = load(p)
s = rep(s, nl, "| T5 follow-ups, WR-17, owner's rename | The lit shader is 6000 lines with a 27-light loop inside; under RT it should carry none of it. | medium |",
       "| T5 follow-ups, WR-17, owner's rename | The lit shader is 6000 lines with a 27-light loop inside; under RT it should carry none of it. | medium — **✅ done 2026-09-06 late evening, record below** |")
s = rep(s, nl, "| **RT-13** | **Transparent surfaces under RT** (the car's glass, OIT): still lit and traced inside their own shader; decide whether a thin G-buffer layer or the in-shader path serves them. | new | The one surface class left outside the G-buffer after RT-8. | small, decide first |",
       "| **RT-13** | **Surfaces outside the G-buffer join it: skinned, layered (terrain), and transparent (the car's glass, OIT).** RT-1 found the first two on the bridge -- the G-buffer pass draws only the plain and masked kinds, so under the direct-light signal the terrain and the characters had no light in the pass and none from the loop; they keep the loop for now (`RV_SKINNED` / `RV_LAYERED` compile without the signal's inputs). The fix is a G-buffer variant per kind and their draw in the G-buffer pass; transparent surfaces decide between a thin layer and the in-shader path. | RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | medium (skinned + layered); transparent: decide first |")
record = """## Records

### RT-1 — ✅ done 2026-09-06, late evening (uncommitted, both copies staged)

**What was built.** (1) Under the direct-light signal the opaque lit shader's light loop walks nothing (`total = 0`); the field's loss (a static surface under a fully baked or hybrid lamp with a moving object in range: one moving-only ray, the loss clamped to what the field holds through the ambient Fresnel and the coherent highlight) moved into `direct_trace.rvshader`, which reads `VolumeIrradiance` and `BakedShare` like the loop did; the G-buffer's id lane packs the shading roughness (integer part over 65535 -- ten bits moved chrome's highlights) with the material's occlusion (the fraction) for the clamp; the loss walk is skipped in a cell whose live sublist is its whole list. (2) The survivors' rays are de-duplicated (two reservoirs keeping one lamp trace once) and the score is cheap (irradiance times the lobe's peak at normal-incidence Fresnel, no masking). (3) `Lamps` → `RaysPerPixel`, `--light-sampling` → `--rays-per-pixel=K[,target]` (the old key still read -- the first landing's text replacement ate the alias, found by the ray count). (4) WR-16 S1's `--shadow-budget` (`RV_SHADOW_BUDGET`, four shader spans) and S4's `--shade-lights` (on screen and at hits) removed: fields, parsing, help, define, bits. `lights per fragment` counts once.

**Measured, garage 1600x900 (T5's numbers in brackets):** lit pass **1.0–1.1 ms** (2.2); K = 4 frame **10.0 ms** (10.7; the loop 11.9), DirectTrace 1.42; K = 8 frame 10.6, DirectTrace 2.16 (2.16 -- the rays halved, 10.9 M → 5.0 M, but the eight full shades cost what they cost: the sampler's remaining lever is fewer shades, not a cheaper score); shadow rays at K = 4 **3.35 M** (5.42) beyond the hits' 7.05 M. **Parity:** the raw pass at every light against the loop **0.0335** mean (T5: 0.034); through the contract 0.155 (with T5's tuning 0.189) -- the accumulator's own effect on a converged still (bilinear history reads under the jitter, the bound), **RT-5's**, and the loop itself is bit-identical before and after the edits (0.0000).

**The bridge, every light, one frame at 60, on against off:** Deck 0.039, Pier 0.072, **Headland 0.048 -- after a T5 defect the garage could not show:** the first Headland read 0.721 with the terrain and the far hills 0.38 darker, because **the G-buffer pass draws only the plain and masked kinds**, so the skinned and layered surfaces had no light in the pass and, under the signal, none from the loop either. They keep the loop now (their pipelines compile without the signal's inputs, and their sets carry no such binding); drawing them into the G-buffer is RT-13. Frame time on the bridge at the preset's K = 8: Headland 14.36 against 14.34 ms, Pier 12.43 against 12.52 -- no gain, because the bake carries the bridge's lamps and few pixels trace a ray there (3.7 M against 4.6 M); the win the design promised for 78-light pixels waits for a live-lit many-light scene.

**Open, into the RT series:** the contract's 0.15 on a converged still (RT-5); skinned and layered into the G-buffer (RT-13); K = 8's cost is in the eight shades (RT-9's per-tile K and RT-10's reuse are the levers).

## The S series, for the record (owner asked 2026-09-06)

WR-16's own build sequence (`docs/RAY-BUDGET-DESIGN.md` Part IV), the oldest of the lists, and where it stands:

| step | what | status |
|---|---|---|
| S0 | the ray counters, the report lines, the debug views `rays` / `lights` / `confidence` / `importance`, the calibration | ✅ done 2026-09-04 (0.85 ms, bit-identical) |
| S1 | the pre-check: K shadow rays a pixel by weighted reservoir sampling, no reuse | ✅ measured 2026-09-04 (the verdict that shaped S4); **its instrument `--shadow-budget` removed in RT-1** |
| S2 | the cheap hit walk: 16-byte cull records, the per-cell live sublist | ✅ done 2026-09-04 (diff of zero; the bridge −8–9%) |
| S3 | the budget: the allocator's stability (averaged demand, dead band, dwell) | ✅ first half built and passing 2026-09-05 (`50e11b7`); **the eight lanes and the controller not built → RT-9** |
| S4 | the shadow spender: the sampler (a), the water's choose/shade passes (b), the light averaged (c), the world-space lamp grid | ✅ complete 2026-09-04 on the water (the choice reuse measured as a loss and off); **on land it is T5 + RT-1 now; the reuse done properly is RT-10** |
| S5 | the water's mirror and refraction rays at half resolution with a hit-distance reconstruction | ✅ built (the sea's reflection at half resolution, four taps weighted by ray distance; WR-18's rays); **folds into RT-8 when the water joins the G-buffer** |

Nothing in the S series is left to build on its own: what remains of it lives in RT-8, RT-9 and RT-10.

"""
s = rep(s, nl, "## After the RT series: the WR items to revisit for the G-buffer", record + "## After the RT series: the WR items to revisit for the G-buffer")
save(p, s)

p = 'docs/HANDOFF.md'; s, nl = load(p)
old = "**Read this first.** Updated 2026-09-06, late evening."
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:i] + "**Read this first.** Updated 2026-09-06, night. The engine is going RT-first. **The one list is `docs/RT-SERIES.md`**; **RT-1 is done** (its record and the S-series status are in that file; the owner asked for a halt after RT-1 and the S list). T1–T5 records stay in `docs/RT-FIRST.md`. Nothing is committed; both builds and both staged shader folders are at the RT-1 state (`--direct-signal=off` is the old path, `--rays-per-pixel=K` the dial, `--light-sampling` its old name). **Waiting on the owner's instructions -- do not start RT-2 without the green signal.** The ninth entry below is the reflection pipeline's state." + s[j:]
save(p, s)

p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_reflection_smear.md'; s, nl = load(p)
old = "**The owner folded the T and R series into one RT series: `docs/RT-SERIES.md`; waiting on their instructions after they read it -- do not start RT-1 unasked.**"
assert s.count(old) == 1
s = s.replace(old, "**The owner folded the T and R series into one RT series: `docs/RT-SERIES.md`. RT-1 DONE 2026-09-06 night (loop gone under the signal, the loss in the pass, ray dedupe, `Lamps` -> `RaysPerPixel` / `--rays-per-pixel` with `--light-sampling` as alias, S1/S4 instruments removed): raw parity 0.0335, bridge 0.04-0.07 on three cameras; the T5 defect found: skinned and layered kinds are outside the G-buffer pass and keep the loop (RT-13). The S series is closed on its own (S0-S5), its remains live in RT-8/9/10. Owner asked to halt after RT-1: do not start RT-2 unasked.**")
save(p, s)
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
old = "the list is now docs/RT-SERIES.md, wait for the owner's word before RT-1; "
assert s.count(old) == 1
s = s.replace(old, "RT-1 DONE too (docs/RT-SERIES.md has the record + the S-series status); wait for the owner's word before RT-2; ")
save(p, s)
print('RT-1 record done')
