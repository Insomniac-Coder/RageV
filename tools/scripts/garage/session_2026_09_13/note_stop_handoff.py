# -*- coding: utf-8 -*-
"""The stop-point hand-off, 2026-09-13 late night: written into docs/HANDOFF.md, CRLF kept."""
import io, sys

PATH = 'C:/Users/ism19/Code/RageV/docs/HANDOFF.md'

ENTRY = '''## 2026-09-13 (late night): validation cleanup mid-way, and the anti-lag to be replaced by measuring change -- STOPPED HERE

**The owner stopped the session here (the five-hour limit) and said: do not revert any changes.**
Everything below is exactly as it was left.

### State of the repository

- `main` is **2 commits ahead of `origin/main`, not pushed** -- push only on the owner's word:
  - `e71cf0c` scenetest green on both backends (the five standing failures: black stand-ins
    for the resolve's guide/material bindings, `u_ScreenReflectionSurface` declared only under
    `RV_RAY_REFLECTIONS` with the new `RHIResourceSet::HasBinding`, the runtime physics check
    counting the scene's own bodies). Vulkan 2491 / OpenGL 2432, none failing.
  - `8d2a318` RT-5 part 5, first half: the young-history blur passes skipped wherever the
    young radius is under half a texel (reflections, direct pair, sea lamps, sea mirror --
    they were exact copies; 0.279 ms of GPU in the garage), one descriptor set per blur stride
    (the rewrite-after-bind tripwire: 576 reports -> 0), `taa_guide` no longer pushes into a
    layout with no push range. Garage bit-identical, bridge Deck bit-identical to the RT-20
    build. **Its message is wrong on one point**: the 3 bridge pixels that moved by one level
    on `e71cf0c`'s build are *not* the include change -- this build has the same include and
    matches RT-20 exactly. Cause unknown (build-level; possibly the undefined behaviour the
    tripwire reported).
- **Uncommitted in the working tree, built into the current Release binaries, NOT yet
  verified by scenetest or a pixel-identity run:**
  1. `VulkanDevice.cpp` -- the validation callback prints the named objects and the command
     buffer's label stack after each error/warning (`objects: ...`, `recorded in: ...`). This
     is what found everything below; keep it.
  2. `Renderer.h/.cpp`, `ReflectionProbe.h/.cpp`, `Scene.cpp` -- the probe face's attachment
     formats come from `Renderer::GetTargetAttachmentFormats()` instead of a hand copy that said
     the surface lane was RGBA8 after it became RGBA16F (VUID-...-08910 on every probe draw,
     `Renderer3D.pbr` in `Reflection probe face`). `ReflectionProbe::MatchesTarget()` replaces
     the samples-only rebuild test.
  3. `Renderer3D.cpp` `FlushBlended` -- the water set writes the lamp probe (binding 7) only
     `if (waterSet->HasBinding(7))`: the surface-only pass has no probe block, and the write was
     what crashed the Khronos layer on the bridge.
  4. `reflection_accumulate.rvshader` -- `o_Motion`/`o_Ident` declared and written only for
     the specular variant (`#elif !defined(RV_SIGNAL_DIFFUSE)`), as its comment always said; the
     occlusion and GI accumulates wrote lanes their targets do not have.
  5. `EngineConfig.h/.cpp`, `Renderer3D.cpp` -- `--ao-blur=<texels>` / `--gi-blur=<texels>`
     measurement flags (negative = tuning, 0 skips the blur passes) for part 5's second half.
     Unmeasured.
  6. `tools/scripts/garage/session_2026_09_13/rt5p5_blur.py` -- that measurement, written, not
     run; and this note's own script.
  The owner's editor-resaved `showroom.rage` (+ .meta) is still theirs, never ours to commit.

### Validation, where it stands (with the uncommitted changes built)

- **Garage (`showroom.rage` from HEAD), 72 frames under `--validation=on`: zero messages.**
- **Bridge Deck camera: runs to the end under validation now (it crashed before), two message
  kinds left, both found and not yet fixed -- the owner's last instruction was to focus on these
  byte/format mismatches:**
  1. `VUID-vkCmdDraw-mipmapMode-04770` in `Renderer3D.direct.water.shade`: set 3 binding 4
     `u_ChoiceIn` is `R32G32B32A32_UINT`, bound with `s_Data->PointSampler`, whose `SamplerDesc`
     leaves `Mipmap` at its default `Linear`. **Planned fix:** `exact.Mipmap =
     MipmapMode::Nearest` where `PointSampler` is created (`Renderer3D.cpp`, the `SamplerDesc
     exact` block near line 1505) -- a point sampler should not blend mips anyway; then prove the
     garage and bridge bit-identical, since it touches every point-sampled read.
  2. `VUID-vkCmdPushConstants-offset-01795` recorded in `WaterAccumulateLamps`: the C++ pushes
     `LampPushConstants` (112 bytes: mat4 + History + Probe + Trace) and
     `water_accumulate.rvshader`'s `LampParams` declares 96 (no `Trace`). **Planned fix:** add
     `vec4 Trace;` (unused) to that block, as `include/water_lamps.glsl` and
     `reflection_trace.rvshader` already declare it.
- After those: rebuild the whole Release config, `scenetest` on both backends (from its own
  directory), garage + bridge bit-identity against `8d2a318`'s frames
  (`rt5b_rt5p5_park_*`, `build/rt5/rt20/bridge/deck_rt5p5_*`), both validation runs clean, commit.

### RT-5 part 4, the anti-lag: measured, fails, and the owner chose its replacement

- Renders done (`rt5p4_antilag.py run fade|park|bridge 0 2 3`); **only `fade` analysed**
  (`rt5_fade_analyse.py al_fade_0 al_fade_N 470 899 8.3`). `park` and `bridge` frames exist and
  are unanalysed (`rt5p4_antilag.py analyse park|bridge 0 2 3`).
- The lamps fade faster -- 95% gone at 90 frames (N=2) and 122 (N=3) against 186 off -- **but a
  still picture never settles**: before the switch the region changes 2.89 levels a frame at
  N=2 and 1.87 at N=3 against 0.48 off, and the picture sits 3-4 levels from the off arm. The
  owner watched the test windows: "there is so much jitter and noise".
- **Why, from the code** (not yet confirmed by painting where it fires): (a) the resolve compares
  one noisy sample against the pixel's history at 2-3 of its own deviations, which plain noise
  crosses a few percent of frames; (b) a reset sets `frames = 1`, which collapses the moments to
  one sample, so the next frame's noise estimate is the 0.02 floor and it fires again -- a pixel
  that trips can stay in a reset loop; (c) the accumulator compares its history with the fresh
  3x3 mean, which differs legitimately at shadow edges; (d) both reset whole, so every false trip
  is a flash of raw noise.
- **Owner's decision: "Measure real change instead of guessing"** -- temporal gradients in the
  manner of A-SVGF (Schied et al. 2018): each frame re-shade a sparse stratified subset of
  pixels (about 1 in 9) with *last frame's* random numbers and surface sample, difference it
  against last frame's value (identical randomness, so the difference is real change, not
  noise), filter the sparse gradients into a dense map, and use it to shorten the accumulators'
  memory and the resolve's still feedback smoothly where light really changed. **Next step is a
  written design against this engine** (which signals first -- direct light, then reflections;
  how the trace passes take the previous frame's salt; forward projection vs same-pixel
  re-shade under the jitter; where last frame's fresh values are kept; the gradient filter; the
  cost), brought to the owner before building. The current `--anti-lag` code in
  `reflection_accumulate` and `taa_resolve` is to be deleted once the replacement lands.

### Also open, found this session

- RT-5 part 5 second half: occlusion and GI blurs off against on (flags and script ready).
- A thin rim round each garage ceiling tube reports motion while parked (RT-20's rule skips it).
- The chrome cube's vertical stripes after it passes the car -- in the shipped resolve too.
- The 3 bridge pixels that flip by one level between builds (see `8d2a318` above).
- RT-18 (coverage mask), RT-16's design call (now to be answered by the gradient work), the
  highlight bound, `water_foam` / `irradiance_fill` rounding.

'''

HEADER_OLD = ('**Read this first.** Updated 2026-09-13 (night): **the entry below headed "the edge flicker was\n'
              'RT-6 refusing the jitter" is the current hand-off** (RT-20 pushed, scenetest green),')
HEADER_NEW = ('**Read this first.** Updated 2026-09-13 (late night): **the entry below headed "validation\n'
              'cleanup mid-way" is the current hand-off** -- the owner stopped the session mid-task and said\n'
              'not to revert anything; it lists the unpushed commits, the uncommitted and unverified changes,\n'
              'the two validation errors left, and the anti-lag decision.\n\n'
              '**Superseded header.** Updated 2026-09-13 (night): **the entry below headed "the edge flicker was\n'
              'RT-6 refusing the jitter" is the current hand-off** (RT-20 pushed, scenetest green),')
ANCHOR = '## 2026-09-13 (night): the edge flicker was RT-6 refusing the jitter -- RT-20 fixed, and scenetest green\n'

raw = io.open(PATH, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')
for old in (HEADER_OLD, ANCHOR):
    if s.count(old) != 1:
        sys.exit('matched %d: %r' % (s.count(old), old[:60]))
s = s.replace(HEADER_OLD, HEADER_NEW).replace(ANCHOR, ENTRY + ANCHOR)
io.open(PATH, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
b = io.open(PATH, 'rb').read()
print('HANDOFF written; CRLF %d, bare LF %d' % (b.count(b'\r\n'), b.count(b'\n') - b.count(b'\r\n')))
