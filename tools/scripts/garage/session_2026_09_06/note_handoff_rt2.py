"""The hand-off for a wiped context (owner's instruction 2026-09-06, late
night, at the usage limit): the state after RT-2, the recipes, the flags,
the traps, the decisions waiting on the owner, where to resume. Into
HANDOFF.md as its tenth entry (above the ninth), NEXT.md, and memory."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

entry = """## 2026-09-06, late night: RT-first T1–T5, RT-1, RT-2 built and measured -- UNCOMMITTED, halted at the usage limit, context wiped after this

**Read this first, then `docs/RT-SERIES.md` (the one list, with the records of RT-1 and RT-2 and the S-series status), then `docs/RT-FIRST.md` §2c–2d (T4 and T5's records).** Everything is uncommitted (195 modified files by `git status`; committing and pushing stay the owner's call). Both builds (`build/bin/Release/RageVRuntime`, `RageVEditor`) and both staged shader folders match the source at the RT-2 state (verified with `cmp` on every shader touched: `direct_trace`, `reflection_accumulate`, `reflection_blur`, `pbr_skinned`, `debug_view`, `include/pbr_fragment.glsl`). No process was left running; no `showroom_burst.rage` copy is left in the scenes folder.

### Where things stand

- **The owner's directive:** the engine goes RT-first; everything RT reads the G-buffer; the T and R series are folded into the RT series (`docs/RT-SERIES.md`); the S series (WR-16's) is closed on its own, its remains live in RT-8/9/10; the WR series (18 items, some to revisit for the G-buffer) comes after the RT series. The protocol: one item per green signal, report after each, solo, propose architecture before building; the owner's decisions are deliberate; explain before executing anything they question.
- **Done and measured:** T1 (G-buffer / lit split), T2 (greying), T3 (ids), T4 (the reconstruction contract), T5 (the direct light as a signal), RT-1 (the lit shader walks no light under the signal; the field's loss in the pass; ray de-dup; `Lamps` → `RaysPerPixel`; the S1/S4 instruments removed), RT-2 (the skinned and layered kinds into the G-buffer; the ambient occlusion as a signal applied to ambient only).
- **Next:** RT-3 (GI as a signal this frame), **only on the owner's green signal**. The order and the complexity of every item are in RT-SERIES.md.

### Last status and the questions put to the owner (they said they will look later; do not act on these unasked)

**Last status:** RT-2 reported as done at 2026-09-06 late night; halted on the owner's word at the usage limit with the context wiped; nothing reverted, nothing committed; the garage and the bridge run clean on the RT-2 build under rays and in raster.

**The questions, exactly as asked:**
1. **The AO look.** The occlusion signal now darkens the ambient terms only; the old chain darkened the whole frame. On the garage the lamps' pools on the graffiti wall are +8 levels of 25, the poles +7.7, the car +1.1, the floor unchanged (`build/garage_burst/rt2_ao_diff_signed_x8.png`). Physically the occlusion belongs to the ambient terms under RT. **Do you accept the new look, or do you want the same signal applied to the direct light too (one multiply)?**
2. **Everything in the G-buffer has a price on terrain.** With the skinned and layered kinds in the G-buffer the pending kinds are rasterised twice; Headland's 4.6 M-triangle terrain took the G-buffer pass from 0.08 to 3.3 ms (the old path 14.4 → 18.1 ms). **Do you want the lit pass to resolve the plain kinds from the G-buffer (a deferred resolve, the question from step 1, now with a number) taken up inside the RT series, or left for the WR revisit as filed?**
3. **RT-3 is next on the list** (GI as a signal this frame). **Green signal?**

### Decisions waiting on the owner (RT-2's record has the numbers)

1. **The AO look.** The signal darkens the ambient terms only; the old post chain darkened the whole frame. On the garage the lamps' pools on the graffiti wall are +8 levels of 25, the poles +7.7, the floor unchanged (`build/garage_burst/rt2_ao_diff_signed_x8.png`). Physically right for RT; if the old look is wanted, applying the same signal to the direct light is one multiply in `pbr_fragment.glsl` (where `screenOcclusion` is applied, ~line 5350).
2. **The G-buffer's cost on terrain.** With every opaque kind in the G-buffer, the pending kinds rasterise twice; Headland's 4.6 M-triangle terrain took the G-buffer pass from 0.08 to 3.3 ms (the old path 14.4 → 18.1 ms). The lever is a lit pass that resolves the plain kinds from the G-buffer (deferred) instead of rasterising again -- step 1's question, now with a number. Filed under the WR revisit.
3. **K = 8 costs what every light costs** in the direct pass (the eight full shades); RT-9's per-tile K and RT-10's reuse are the levers.

### The recipes (all from `C:/Users/ism19/Code/RageV`, Git Bash)

- **Build:** `"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target RageVRuntime --target RageVEditor` (~2 min). **Shaders are not built -- stage them:** `cp RageVEditor/assets/shaders/<file>.rvshader build/bin/Release/{RageVRuntime,RageVEditor}/assets/shaders/` and the include `include/pbr_fragment.glsl` into `.../assets/shaders/include/`. A stale staged copy is the first suspect for any "nothing changed" result.
- **Benchmark (garage):** `cd build/bin/Release/RageVRuntime && ./RageVRuntime.exe --project=C:/Users/ism19/Code/RageV/SampleProject --scene=scenes/showroom.rage --rhi=vulkan --render-defaults=off --vsync=off --width=1600 --height=900 --benchmark=60 --import-cache=off --camera=-2.3,0.72,-2,11,0,4 [flags] > log` -- read `frame  mean`, the "render graph, by pass" table, `rays per frame`, `lights per fragment`, and `grep -c "Shader compilation failed"` (must be 0) and `grep "\\[Vulkan\\]"` (must be empty).
- **Captures (garage):** `python tools/scripts/garage/burst.py <tag> --speed=0 --stop=0.1 --frames=20 --from=150 --extra=<flag>...` for 20 parked frames `<tag>_150..169`; `--speed=1.5 --stop=2.0 --frames=160 --from=30` for the dolly (frames 30..189, moving to ~120); `--parked <tag> --extra=--debug-view=...` for one frame. Output in `build/garage_burst/`. burst.py needs the garage's orbit script (`ShowroomCamera`) and cannot run the bridge.
- **The bridge (no burst.py):** the runtime directly with `--scene=scenes/GoldenGateDemo.rage --screenshot=<png> --screenshot-frame=60 --screenshot-count=1 --frame-time=0.0166 --camera=<pose>`; poses in `tools/scripts/bench_night.py` (`CAMERAS`): Headland `500,89.47,-1100,0.01,-157.08,8.88`, Deck `0,76.4,950,0.01,0,0`, Pier `70,4.5,705,0.01,-46.98,-2.86`, Glitter `500,2.5,180,0.01,-90,-1.146`.
- **Metrics:** `tools/scripts/garage/session_2026_09_06/parked_stats.py <arm>` (per-frame change 170-189 + drift), `smear_metric.py <arm>`, `edge_shake.py <arm>_still`; the region boxes used in every comparison (fractions of 2000x1230 mapped to the capture size): floor y 880-1180 x 300-1700, car y 560-760 x 560-900, wall y 250-550 x 1020-1220, poles y 250-800 x 1100-1500, tubes/ceiling y 0-250 x 300-1700. Diff images: signed mean over frames, x8 or x16, green = arm brighter, red = darker.
- **Reference arms and their captures:** `r5_still` / `r5_dolly` (the accepted reflection state), `t5_off_still` = `rt1_off_still` (the old lit loop, every light -- bit-identical across RT-1's edits), `rt1_raw_still` (the direct pass with the accumulator bypassed: 0.0335 against the loop), `rt1_bridge_*_off.png` / `rt2_bridge_headland_*.png`, `rt2_ao_on/off_still`.

### The flags that matter now

`--direct-signal=on|off` (the direct light as a signal / the old loop), `--ao-signal=on|off` (the occlusion signal / the old post chain), `--rays-per-pixel=K[,target]` (K lights a pixel on land and water; 0 = every light -- **on the bridge 0 also turns the water's lamp passes off and the sea walks every lamp in its own shader: 28 ms; use the preset for timing**; `--light-sampling` is the old name, still read), `--rt-reflections=off`, `--ray-tracing=off` (raster; SSAO rides the signal path too), `--debug-view=direct-light|direct-refusal|ao|reflection|reflection-refusal|reflection-picture|rays|lights`. **`--reflection-history=off` is not a contract bypass** (with the pair it produced +41 levels on the floor); to see a signal raw, stage a one-line bypass in `reflection_accumulate.rvshader` (`kept = fresh.rgb; kept2 = fresh2.rgb; frames = 1.0;` before the writes, under `RV_SIGNAL_PAIR`) into the runtime copy only, capture, restore -- and only on a build whose young blur is off for that signal, or the blur at one frame of history smears the test.

### What is where (the code of T5, RT-1, RT-2)

`RageVEditor/assets/shaders/direct_trace.rvshader` (the direct pass: K lights by reservoir on a cheap score, the field's loss and its clamp, ray de-dup, the counters' flush); `include/pbr_fragment.glsl` (set 0 bindings 26/27 direct pair and 28 occlusion under `RV_DIRECT_SIGNAL_INPUT` / `RV_SCREEN_OCCLUSION_INPUT`; the `RayRates.w` bits 22 and 23; `total = 0` under the signal; `ClusterCellFor`; the G-buffer's `o_Albedo.a` = specular scalar, `o_SurfaceId` = (signed id, floor(shadingRoughness*65535) + occlusion); the S1/S4 instruments gone); `reflection_accumulate.rvshader` / `reflection_blur.rvshader` (`RV_SIGNAL_PAIR`, the twin's memory from `Tuning.w`); `Renderer3D.cpp` (`TraceDirectLight`, `SetDirectLight/SetDirectSignal/SetAoSignal/SetScreenOcclusion`, `DirectSignal()`/`AoSignal()`, the pair pipelines as passes 6/7, the skinned/layered/pending G-buffer sets and `DrawGBufferPending`, the six lit-kind sets re-committed in `DrawLit`); `FrameGraphBuilder.cpp` (`addSignal` hoisted above the G-buffer pass; the DirectTrace → accumulate → blurs chain; the Occlusion compute → upsample → accumulate → blurs chain; the budget map's Prepare and imports hoisted; the debug views); `EngineConfig` (`DirectSignal`, `AoSignal`, `RaysPerPixel*`, the views); `RenderSettings.h` (`RaysPerPixel` in the preset); `TemporalHistory` (a fourth attachment); the layers' `DirectLight` slots. Every patch is a script in `tools/scripts/garage/session_2026_09_06/` (`patch_rt_t5a..g`, `patch_rt1a/b`, `patch_rt2a/2a2/2b`, the `note_*` docs scripts) -- **they are records, not tools to re-run: several applied in halves and the files on disk are the truth.**

### Traps paid for this session (read before patching anything)

- **Anchors from comment-stripped listings fail:** every `grep -v "//"` listing hid comment lines that sit between the lines you anchor on. Anchor on single lines, or read the raw text (`cat -A`) first. A block anchor that fails after an earlier `rep` in the same script leaves the earlier files saved and the rest unapplied -- **never re-run a half-applied script whole; guard each section (`if 'marker' in s`) or split it.**
- **A global text replace eats what you just inserted:** `s.replace('light-sampling', 'rays-per-pixel')` rewrote the alias line added a few lines earlier; found only because the ray count said K = 8 where K = 4 was asked. After a rename, grep for the alias.
- **`Renderer3D.cpp` is CRLF throughout** (7454/7454 lines), with a blank line between `DrawLitBody(...)` and its closing brace; `pbr_fragment.glsl` has mixed endings in places (`t4_prelude.py`'s `rep` tries both).
- **Descriptor sets:** the RHI errors when a set is rewritten after being bound in this frame's command buffer (`VulkanPipeline.cpp` ~644); set layouts come from reflection, so a binding a variant never references is not in its layout and `SetTexture` on it is an error -- guard declarations per variant and set only the sets whose pipelines declare them. The G-buffer pass binds only the G-buffer sets, so the lit-kind sets can be re-committed in `DrawLit`.
- **The pending draws' G-buffer sets must index the CPU visibility list** (`slot.Visible`), not the GPU cull's (`IndirectView.Instances`); and `TransparentBegin` is computed by the lit draw, so a G-buffer-half helper must find the opaque range itself. The first landing drew nothing for exactly these two reasons.
- **The lit shader's specular-antialiased roughness is not in `o_Surface`** on purpose; the id lane carries it (16 bits; 10 bits moved chrome by three levels).
- **The young-history blur smears hard shadow edges; the specular twin wants a short clamped memory; the accumulator's jitter handling is right as it is** -- all three measured, T5's record.
- **The flat-pixel per-frame change during a 1.5 m/s dolly is texture displacement, not noise**: it cannot attribute "the wall's motion noise" to any signal (tried for the direct light and the AO). A reprojected reference is needed (RT-5/RT-6).
- **A counter changes what it counts** stays true: the direct pass had to flush its own ray counters (`FlushRayCounters(false)` under `RV_RAY_COUNTERS`) or its rays were invisible to `rays per frame`.

"""
p = 'docs/HANDOFF.md'; s, nl = load(p)
anchor = "## 2026-09-06, night: one ray + a hit-point resolve, silhouettes kept, the TAA's still feedback, LD sampling -- edge flicker at the no-AA floor, 10.9 ms -- UNCOMMITTED"
assert s.count(anchor) == 1
s = s.replace(anchor, entry.replace('\n', nl) + anchor)
old = "**Read this first.** Updated 2026-09-06, late night."
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:i] + "**Read this first.** Updated 2026-09-06, late night, at the usage limit with the context about to be wiped: **the tenth entry below is the complete hand-off** (state, recipes, flags, traps, the decisions waiting on the owner); `docs/RT-SERIES.md` is the one list with RT-1's and RT-2's records and the S-series status; `docs/RT-FIRST.md` holds T1–T5's records. Nothing is committed. **Next is RT-3, only on the owner's green signal**, after they have judged RT-2's two open decisions." + s[j:]
save(p, s)

p = 'docs/NEXT.md'; s, nl = load(p)
s = s.replace("> **2026-09-06 late evening, owner-set: the T series and the R series are one RT series now — `docs/RT-SERIES.md`. Read it first; it is the order. The WR series (18 items, some to revisit for the G-buffer) comes after it.**",
              "> **2026-09-06 late night: RT-1 and RT-2 are done (records in `docs/RT-SERIES.md`); HANDOFF.md's tenth entry is the complete hand-off for a wiped context. Next is RT-3 on the owner's green signal, after they judge RT-2's two open decisions (the AO look; the G-buffer's cost on terrain). The T and R series are one RT series now — `docs/RT-SERIES.md` is the order; the WR series comes after it.**", 1)
save(p, s)

# memory: one file for the RT-series state, and the index line
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_rt_series_state.md'
open(p, 'wb').write("""---
name: project-ragev-rt-series-state
description: RageV RT-first state as of 2026-09-06 late night -- T1-T5, RT-1, RT-2 done and uncommitted; RT-3 next only on the owner's green signal; two decisions waiting on the owner; the traps of this session
metadata:
  type: project
---

**State (2026-09-06, late night, halted at the usage limit, context wiped):** the engine
goes RT-first; `docs/RT-SERIES.md` is the one list (T and R series folded; the S series
closed, its remains in RT-8/9/10; the WR series after). Done, measured, UNCOMMITTED in
both builds and both staged shader folders: T1 G-buffer split, T2 greying, T3 ids, T4 the
reconstruction contract, T5 the direct light as a signal, RT-1 (the lit shader walks no
light under the signal, the field's loss in the pass, ray de-dup, `Lamps` ->
`RaysPerPixel` / `--rays-per-pixel` with `--light-sampling` as alias, S1/S4 instruments
removed), RT-2 (skinned + layered kinds into the G-buffer -- RT-13's opaque half; the AO
as a signal applied to ambient only, `--ao-signal=off` the A/B). `docs/HANDOFF.md`'s
tenth entry is the complete hand-off: recipes, flags, traps, what is where.

**Next: RT-3 (GI as a signal this frame), only on the owner's green signal, after they
judge two things RT-2's record puts to them:** (1) the AO now darkens ambient only -- the
garage's lit graffiti wall is +8 levels of 25 against the old whole-frame multiply (one
multiply restores the old look); (2) every opaque kind in the G-buffer rasterises the
pending kinds twice -- Headland's terrain +3.3 ms (old path 14.4 -> 18.1 ms), the lever
is a deferred resolve of the plain kinds, filed under the WR revisit.

**Why:** the owner runs one item per green signal, reports after each, decides looks and
costs themselves; a fresh session must not start RT-3 or change the AO look unasked.

**How to apply:** read HANDOFF's tenth entry first, then RT-SERIES.md. Measure with the
recipes there (burst.py for the garage; the runtime directly for the bridge -- never
`--rays-per-pixel=0` for bridge timings, it turns the water's lamp passes off). Anchor
patches on single raw lines (comment lines hide in stripped listings); never re-run a
half-applied script whole; grep for aliases after a rename; descriptor sets cannot be
rewritten after a bind and layouts come from reflection. The flat-pixel per-frame change
during a dolly is texture motion, not noise -- it cannot attribute "the wall's motion
noise" (still unattributed; needs a reprojected reference, RT-5/RT-6).

Related: [[project-ragev-reflection-smear]], [[feedback-report-each-task-green-signal]],
[[feedback-rt-first-engine-not-scene]], [[feedback-owner-settings-are-deliberate]].
""".encode('utf-8')); print('ok', p)
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
line = "- [RT-series state (2026-09-06 late night)](project_ragev_rt_series_state.md) — T1-T5, RT-1, RT-2 done and uncommitted; RT-3 only on the green signal; two decisions wait on the owner (the AO look, the G-buffer's terrain cost); HANDOFF's tenth entry is the full hand-off"
if 'project_ragev_rt_series_state.md' not in s:
    s = line + nl + s
save(p, s)
print('hand-off written')
