"""RT-2 done: the record into docs/RT-SERIES.md, the rows, the hand-off header,
memory."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == 1, (s.count(o), old[:70]); return s.replace(o, n)

p = 'docs/RT-SERIES.md'; s, nl = load(p)
s = rep(s, nl, "| T6 | The wall's motion noise the owner saw is most likely this: RTAO's per-frame sample with a history TAA invalidates under motion. The garage's direct light cannot be it (hard shadows, measured). | medium |",
       "| T6 | The wall's motion noise the owner saw is most likely this: RTAO's per-frame sample with a history TAA invalidates under motion. The garage's direct light cannot be it (hard shadows, measured). | medium — **✅ done 2026-09-06 night, record below; the motion-noise guess did not hold up** |")
s = rep(s, nl, "| RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | medium (skinned + layered); transparent: decide first |",
       "| RT-1's finding, new | Every opaque surface must be in the G-buffer or every signal skips it. | **skinned + layered ✅ done inside RT-2** (see its record); transparent: decide first, small |")
record = """### RT-2 — ✅ done 2026-09-06, night (uncommitted, both copies staged)

**First, RT-13's opaque half, pulled forward because every pre-lit signal needs it.** The G-buffer pass drew only the GPU-culled plain and masked draws; everything in the pending list -- the skinned and layered kinds, and on the bridge 184 of its 201 draws, the terrain among them -- was outside the G-buffer. Now: `pbr_skinned` / `pbr_layered` compiled with `RV_GBUFFER`, their pipelines and sets (uploaded like their lit sets: the CPU visibility list, instances, bones), plus a pending-draw pair of G-buffer sets for statics and masked draws indexed by the CPU list (the indirect G-buffer set carries the GPU cull's), and `DrawGBufferPending` -- DrawLitBody's vertex path with the G-buffer pipelines -- in the split's G-buffer half. The first landing drew nothing (it read the opaque range from `TransparentBegin`, which the lit draw computes later); the second finds the range itself. **Headland, every light, on against off: 0.064** (RT-1's fix had the terrain keep the loop; now the pass lights it), the loop bit-identical. **The cost is real:** the pending kinds are rasterised twice, and Headland's terrain is 4.6 M triangles -- the G-buffer pass went from 0.08 to 3.3 ms there, the frame at the old path from 14.4 to 18.1 ms. That is the price of "everything in the G-buffer" on a terrain-heavy view under forward+ with a prepass; the lever is a lit pass that resolves the plain kinds from the G-buffer instead of rasterising them again (the deferred question of step 1, now with a number), or a cheaper terrain LOD for the G-buffer half. Filed under the WR revisit, not fixed here.

**Then the signal.** `OcclusionCompute` (RTAO under rays at half resolution reading last frame's budget map, imported early; SSAO at its rung in raster) from the G-buffer's depth and normal before the lit pass; `OcclusionUpsample` -- the old apply shader against a white scene, which is exactly its depth-aware upsample with the intensity folded in, so the contract runs at the G-buffer's size and the lit shader reads by texel; the contract as `OcclusionAccumulate` + three blurs (`AoSignal()`: diffuse kind, slot 2, young blur 6 texels); the lit shader reads set 0 binding 28 under `RayRates.w` bit 23 and multiplies **the ambient, the stored indirect and the environment's specular only** -- never the direct light. `--ao-signal=off` keeps the old post-apply chain as the A/B; `--debug-view=ao`. Works in raster too (SSAO through the same path).

**Measured, garage:** frame 10.8 ms against 10.1 with the post chain (the contract at full resolution costs 0.85 ms where the old chain cost 0.27); no errors under rays or raster. **The picture changes as designed, and it is a look change the owner should judge:** where the lamps' cones hit the graffiti wall the old chain darkened the direct light by the AO, the new one does not -- the wall +8.2 levels (of 25), the poles +7.7, the car +1.1, the floor +0.06, the ceiling +0.4; the diff image (`rt2_ao_diff_signed_x8.png`) is the lit pools on the wall and under the poles, nothing else. Physically AO belongs to the ambient terms; if the old look is wanted, applying the same signal to the direct light too is one multiply. On a still the signal's per-frame change is 0.80 on the wall against the old chain's 0.61 (the contract without the old chain's 0.9 feedback).

**The motion-noise guess did not hold up.** Reflections off, the dolly's flat-pixel per-frame change is the same with the AO signal on and off (wall 11.30 against 11.43, floor 9.17 both), as it was for the direct light in RT-1: at 1.5 m/s that metric is texture displacement, not noise, and it cannot attribute the wall's motion noise to any signal. The attribution needs a reprojected reference (RT-5/RT-6's territory); what T5's design named "the wall's motion noise" is still unattributed by measurement.

**The bridge at the preset, both signals against the old path (both with the pending kinds in the G-buffer):** Headland 19.7 against 18.1 ms (the AO contract +0.85, the direct trace +0.76, the lit pass −0.57), Pier 15.6 against 17.5 (the lit pass −1.2, the G-buffer −0.4). Mixed, and both numbers carry RT-13's doubling above.

**Open:** the AO debug view reads near-white on a linear ramp (RT-12's log ramp); the transparent kinds are still outside the G-buffer (RT-13's remainder); the doubled rasterisation of the pending kinds (WR revisit).

"""
s = rep(s, nl, "## The S series, for the record (owner asked 2026-09-06)", record + "## The S series, for the record (owner asked 2026-09-06)")
save(p, s)

p = 'docs/HANDOFF.md'; s, nl = load(p)
old = "**Read this first.** Updated 2026-09-06, night."
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:i] + "**Read this first.** Updated 2026-09-06, late night. The engine is going RT-first. **The one list is `docs/RT-SERIES.md`**; **RT-1 and RT-2 are done** (records and the S-series status in that file; RT-2 also drew the skinned and layered kinds into the G-buffer, RT-13's opaque half). Nothing is committed; both builds and both staged shader folders are at the RT-2 state (`--direct-signal=off`, `--ao-signal=off` are the old paths; `--rays-per-pixel=K` the dial). **Waiting on the owner's instructions -- do not start RT-3 without the green signal.** The AO now darkens ambient only, a look change the owner should judge (RT-2's record has the numbers). The ninth entry below is the reflection pipeline's state." + s[j:]
save(p, s)

p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_reflection_smear.md'; s, nl = load(p)
old = "Owner asked to halt after RT-1: do not start RT-2 unasked.**"
assert s.count(old) == 1
s = s.replace(old, "RT-2 DONE 2026-09-06 late night: the skinned/layered kinds drawn into the G-buffer (pending draws need their own G-buffer sets on the CPU visibility list; the pending kinds now rasterise twice -- Headland's terrain +3.3 ms, a deferred-resolve question for the WR revisit); AO as a signal from the G-buffer (RTAO/SSAO, the apply shader against white as the upsample, the contract, binding 28, RayRates.w bit 23) applied to ambient only -- the garage's lit wall +8 levels, a look change for the owner to judge; the flat-pixel motion metric cannot attribute the wall's motion noise to any signal. Do not start RT-3 unasked.**")
save(p, s)
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
old = "RT-1 DONE too (docs/RT-SERIES.md has the record + the S-series status); wait for the owner's word before RT-2; "
assert s.count(old) == 1
s = s.replace(old, "RT-1 and RT-2 DONE (docs/RT-SERIES.md has the records); wait for the owner's word before RT-3; ")
save(p, s)
print('RT-2 record done')
