"""RT-3's hand-off: the twelfth entry, and the header pointed at it."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/HANDOFF.md'
s = read(D)
if has(s, '## 2026-09-07, evening: RT-3'):
    print('HANDOFF.md already has the twelfth entry')
    raise SystemExit(0)

s = rep(s,
    "**Read this first.** Updated 2026-09-07, after RT-2.1: **the eleventh entry below is this session's hand-off**, the tenth (2026-09-06) the complete one for the RT-first state (recipes, flags, traps, what is where). `docs/RT-SERIES.md` is the one list with the records of RT-1, RT-2 and RT-2.1. Nothing is committed. **Next is RT-3, only on the owner's green signal.** The AO look is accepted; the deferred resolve is **RT-2.2**, owner-filed for the end of the series.\n",
    "**Read this first.** Updated 2026-09-07 evening, after RT-3: **the twelfth entry below is this session's hand-off**, the tenth (2026-09-06) the complete one for the RT-first state (recipes, flags, traps, what is where). `docs/RT-SERIES.md` is the one list with the records of RT-1, RT-2, RT-2.1 and RT-3. Nothing is committed. **Next is RT-4 (reflections traced from the G-buffer), only on the owner's green signal.** The AO look is accepted; the deferred resolve is **RT-2.2**, owner-filed for the end of the series.\n")

ENTRY = """
## 2026-09-07, evening: RT-3 done -- the bounce is a signal of this frame, and three defects in its own plumbing -- UNCOMMITTED

**Read this, then RT-3's record in `docs/RT-SERIES.md` (it has the audit table, every number and the open items), then the tenth entry below for everything else -- its recipes, flags and traps all still hold.** The owner's instruction this session was "start RT-3". Both builds and both staged shader folders are at the RT-3 state (`cmp` clean on `pbr_fragment.glsl`, `gi_upsample.rvshader`, `gi_denoise.rvshader`, `rtgi_trace.rvshader`); no process left running; no scene copy left in the scenes folder; the probe edits made in the runtime's shader copy were restored and verified with `cmp`.

### What RT-3 did

- **The traced bounce runs between the G-buffer and the lit pass**, is upsampled to the lit pass's grid by a new joint bilateral pass, is settled by the reconstruction contract, and is read by texel at binding 16 -- the same binding the one-frame-late buffer used, with `RayRates.w` **bit 24** saying which of the two is in it. `gi_denoise` and its one frame of latency are gone from the traced path; the screen-space forms keep both, because their gather reads the lit image.
- **`gi_denoise` audited against the contract's four properties** (the table is in RT-3's record). It loses on three -- it has no surface reprojection, no geometric tests and no motion-capped memory, and its own header admits the colour clamp cannot stand in for them on a near-uniform signal. **It wins on one:** its bound accumulates in a range-compressed space and bounds the fresh sample against its neighbours before building the box. That is written down as an open item for RT-5, not merged blind: it changes what all four signals average.
- **The picture is unchanged and the reference arm is bit-identical.** Garage 0.098 levels mean, camp 0.079, no structure in either diff image at x8; `--gi-signal=off` reproduces the pre-RT-3 build exactly (0.0000). The bridge and the baked garage build zero signal passes and are untouched (Headland against RT-2.1's still: 0.034 levels).
- **The cost is +1.06 ms of GI passes on the garage, +0.66 on the camp**, and where it goes is not the trace: the contract's accumulate and three blurs run at *full* resolution on a *half*-resolution signal, so they pay four times the texels they carry information for. Halving them is worth about 0.63 ms and needs the contract taught a scale. **That is a decision waiting on the owner.**
- **The lag RT-3 removes is not visible in either scene**, and the honest reason is that the whole traced bounce is worth +0.383 levels in the garage and +0.588 in the camp. Measured directly: the reference at frame N is forty times closer to the signal at frame N than at N-1.

### Traps paid this session (the first is the one to read)

- **A signal can be computed every frame and read by nobody, and no comparison of finished frames will say so.** The GI signal's intensity was gated on the *old* buffer's history, so the lit shader's branch never ran. The arms still differed, by a plausible-looking 0.799 levels -- entirely the old chain being switched off. **What caught it: a probe writing a bright constant into every texel of the upsample moved the frame by exactly the same 0.799.** That equality is the test. Do it once per new signal, in the runtime's staged shader copy only, before believing any number: `o_Color = vec4(3.0, 0.0, 0.0, 1.0);` and restore with `cp` + `cmp`.
- **The contract's alpha is a frame count; `gi_denoise`'s is a validity flag.** Anything moved from the old buffer onto the contract must be re-read: `o_Accumulated = vec4(kept, frames)` runs to 64, and zero means no surface stood there. The lit shader was multiplying the bounce by it. Same class as 7ay's linear-depth-in-alpha.
- **`SetTexture` with a null texture segfaults** -- no validation message, no `[Vulkan]` line. The moment a "have this" flag can be true before its texture exists, every binding site for it needs the fallback.
- **A guard marker must not be a substring of anything else in the file.** `if has(s, '16777216')` matched `16777216.0`, a hash divisor used eight times in `pbr_fragment.glsl`, and the patch reported itself already applied. Guard on a whole distinctive line.
- **`FrameGraphBuilder.cpp` is mixed LF and CRLF within one file** (the debug-view block is LF, the rest CRLF). `tools/scripts/garage/session_2026_09_07/rep.py` tries both and asserts the match count; every patch this session went through it, and it caught two anchors that would otherwise have silently done nothing.
- **`PostProcess::Shader` has a `static_assert` and a fixed-size array** that must both grow with the enum.

### What changed in code (all uncommitted, on top of the RT-2.1 state)

**New:** `RageVEditor/assets/shaders/gi_upsample.rvshader`. **Changed:** `PostProcess.h/.cpp` (`Shader::GiUpsample` at index 35, the array and assert to 36, `PostProcess::GiUpsample`), `Renderer3D.h/.cpp` (`SetGiSignal`, `SetScreenIndirectSignal`, `GiSignal()` tuning, `GiSignalRequested` as `RayRates.w` bit 24, binding 16 re-committed on the lit sets in `DrawLit`, `haveIndirect` no longer requiring a texture, both binding-16 sites guarded against null), `FrameGraphBuilder.h/.cpp` (`FrameDesc::GiLight`; the `giSignal` gate resolved above the intensity block; the `GI trace` / `GI upsample` / contract chain before the lit pass; the old post-lit chain gated on `!giSignal`; the two debug views and their log names), `include/pbr_fragment.glsl` (the bit-24 branch: `texelFetch` at `gl_FragCoord.xy`, the irradiance taken whole, the count read as validity), `EngineConfig.h/.cpp` (`GiSignal`, `--gi-signal=on|off`, `DebugViewMode::GiLight`/`GiRefusal`, `--debug-view=gi-light|gi-refusal`), `RuntimeLayer.h/.cpp` and `EditorLayer.h/.cpp` (a `TemporalHistory` per view).

**Scripts** (records, not tools to re-run -- `patch_rt3e.py` applied five of its six sections and stopped, and `patch_rt3e2.py` is the sixth): `tools/scripts/garage/session_2026_09_07/{rep,patch_rt3a,patch_rt3b,patch_rt3c,patch_rt3d,patch_rt3e,patch_rt3e2,patch_rt3f,patch_rt3g,patch_rt3h,note_rt3_done,note_rt3_handoff}.py`. Captures and diffs in `build/rt3/`; garage bursts `rt3f_off*`, `rt3f_on*` in `build/garage_burst/`; logs in `build/bin/Release/RageVRuntime/rt3_*.log` and `camp_*.log`.

### The new flags

`--gi-signal=on|off` (the bounce as this frame's signal / the one-frame-late buffer and `gi_denoise`; off is the reference arm), `--debug-view=gi-light` (the settled bounce as the lit shader reads it, ramp 1.0 -- it early-returns and ignores `--debug-view-mix`), `--debug-view=gi-refusal` (why each texel's history was refused, ramp 6.0, the reflections' codes). Both read `currentGi` and draw black with a named warning when the signal is off.

### Last status and the questions put to the owner

**Last status:** RT-3 reported as done; nothing reverted, nothing committed; the garage (baked and forced-realtime), the camp and the bridge all run clean with zero shader errors and no Vulkan messages.

**The questions:**
1. **The contract's resolution.** It runs at full resolution on a half-resolution signal and its four passes are 0.85 ms of the garage's 0.89 ms GI chain. Running them at half and upsampling last is worth about 0.63 ms but needs the contract taught a scale for its G-buffer lookups -- shared code, four signals. **Take the 0.63 ms, or leave the shared code alone?** (RT-2's occlusion signal made the same trade silently; it is the same lever there.)
2. **`gi_denoise`'s bound.** Range compression and a firefly bound on the fresh sample are the one thing the old denoiser did better, and a hemisphere estimate wants both. Filed for RT-5, where the bound is the subject. **Confirm that is the right place, or pull it forward?**
3. **The bounce is worth under 0.6 levels in both test scenes.** That is why RT-3 has no picture to show for itself. It is a question about the scenes or the GI settings rather than about the item -- worth a look, or leave it?
4. **RT-4 is next on the list** (reflections as an instance of the shared code, traced from the G-buffer). **Green signal?**
"""

s = rep(s,
    "\n## 2026-09-07: RT-2.1 done -- the terrain's cost was the parallax march at mip 0, not the raster -- UNCOMMITTED, halted on the owner's instruction after the report\n",
    ENTRY + "\n## 2026-09-07: RT-2.1 done -- the terrain's cost was the parallax march at mip 0, not the raster -- UNCOMMITTED, halted on the owner's instruction after the report\n")
write(D, s)
print('HANDOFF.md: twelfth entry written')
