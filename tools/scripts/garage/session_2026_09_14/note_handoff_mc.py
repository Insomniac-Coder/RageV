# -*- coding: utf-8 -*-
"""The measured-change hand-off, 2026-09-14: written into docs/HANDOFF.md, CRLF kept."""
import io, sys

PATH = 'C:/Users/ism19/Code/RageV/docs/HANDOFF.md'

ENTRY = '''## 2026-09-14: the anti-lag replaced by measuring change -- phases 1 and 2 built and measured, phase 3 and the moving camera next

**Read `docs/RT-MEASURED-CHANGE.md` first** -- the design, the owner's decisions, and every
measurement (sections "Phase 1 measured", "Cost cut", "Phase 2"). Everything below is
**uncommitted, built into the Release binaries, off by default** (`--measured-change=on`), and on
top of the 2026-09-13 late-night entry's uncommitted state, which still stands (do not revert
anything; `e71cf0c` and `8d2a318` still unpushed; the owner's editor-resaved `showroom.rage` is
still not ours).

### The owner's instructions, in force

- **The lighting must not change.** The owner ruled out anything that alters the picture (stable
  light ids in the trace were dropped for that): the check replays, it never re-keys the trace.
- **Both filters** (the signal's accumulator and TAA) are driven by the map.
- **Next, in order:** (1) **phase 3, the bounce light (GI)**; test; **if the tests are good, this
  becomes the engine's anti-lag method** -- then turn it on by default and delete the old anti-lag
  (`--anti-lag`, `--anti-lag-floor`, `EngineConfig::SignalAntiLag*`, the `AntiLag` push lanes and
  the shader blocks in `reflection_accumulate` and `taa_resolve`). (2) **The moving camera is
  non-negotiable**: today the check waits for the camera to stand still (see below) and must not.
  (3) The water (the bridge's sea lamps and sea mirror have their own filters) is for later. The
  lamp lenses are fine as they are (owner looked).
- Explain in few plain words; report after each task; ask before spawning agents.

### What was built

- **Phase 1, direct light** (`direct_trace.rvshader`): `RV_DIRECT_RECORD` (one pixel per 3x3
  block: world position, the G-buffer's surface/albedo/id texels, the trace's luminance, AND the
  reservoir's picks and weights -- eight RGBA32F lanes) and `RV_DIRECT_RELIGHT` (replays those
  picks with this frame's lamp data, the same soft-shadow rays, lamps new this frame added in full).
  Draw keys are macros (`RV_DIRECT_ROLL`, `RV_DIRECT_PIXEL`, `RV_DIRECT_EYE`, `RV_TRACE_PIXEL/FRAME/
  ANIMATED/CAMERA`) that spell the trace's own text when not re-lighting. **Why picks are stored:**
  a fresh reservoir draw read other lamps' resampling as change (bridge beacons).
  `LightRenderData::Id` (entity+1) and the two-half `u_ChangeRolls` map (now->then index, then->now).
- **Phase 2, reflections** (`reflection_trace.rvshader`): `RV_REFLECTION_RECORD/RELIGHT`, four
  lanes; the ray's direction is a sequence and a hit chooses no lamps, so the same ray is traced
  again. **The reflection accumulator's alpha is also the lit shader's probe trust** (fades the probe
  in over 4 frames): with the check on, the blend count rides `o_Ident.g` and the alpha keeps its
  trust -- restarting the alpha brought the old probe image back and TAA held it.
- `change_filter.rvshader` (PostProcess `ChangeFilter`): signed a-trous over the block grid, shares
  at the end, floor 0.02 (`--change-iterations`, `--change-floor`). Accumulate and TAA: frame count
  capped at 1/share. Debug views `--debug-view=change` and `reflection-change`.
- **Cost cut**: `DirectChangeKey` / `ReflectionChangeKey` (Renderer3D.cpp) hash every input the
  re-light reads (lamps, cull records, cluster/grid/field blocks, camera, `RayShadows::
  GetGeometryKey`, and ray-instance materials for reflections). Same key as the record's: no draw,
  no map, record kept. One record target per signal (`MeasuredChangeHistory`, TemporalHistory.h),
  loaded with `RGLoad::Preserve`.

### Measured (numbers in the doc)

- Lights button: frames until 10% / 5% of the lamps' light is left -- today 145 / 186, phase 1
  64 / 118, **phases 1+2: 15 / 63**. What is left is the **GI history** (44% over its settled value 20
  frames after the switch, `--capture-signals`); direct diffuse 1.1%, reflections 0.3%.
- Parked garage: identical to today within its known frame-168 noise; bridge quiet frames identical,
  +2% frame change during beacon flashes (the map fires as a speckled field there -- real beacon light
  seen through one soft-shadow ray; a wider or support-weighted filter is the lever if it shows);
  dolly identical; chrome cube mixed on 0.1% of pixels. Cost parked: **0.045 ms** both phases.
  Validation clean (bridge shows only the two known 09-13 errors); scenetest green both backends.
- Traps paid for: the first run after a rebuild differs from the next (~1 level on 0.1% of the
  bridge) -- compare against a second run; the re-light pipeline's attachment formats must match the
  graph target's (VUID-08910); `Renderer3D.h` declarations must come after `GiTraceView`.

### Phase 3 (GI) -- what to find out first

`rtgi_trace.rvshader` runs on its own grid (half resolution in the garage) through the guidance
downsample (`SignalGuidance`, `Divisor`); check how its ray directions are drawn (hash of pixel and
frame?), whether the ray count per pixel comes from the tile allocator's map (then the record must
keep it), and what the second bounce draws. Same shape as phase 2 if nothing chooses lamps.

### The moving camera -- where the current design stops, and a way through (not built)

Today the record waits for a camera equal to last frame's and the re-light for the record's own
camera, and both keys include the camera. What actually needs the camera still:
1. the key (drop the camera from it; the re-light reads world-space inputs);
2. `ClusterCellFor(P)` in the every-lamp mode and the new-lamp loop (use the world grid,
   `u_WorldCells`, which does not move with the camera -- or keep the record's cell list);
3. the map lives on last frame's block grid: the accumulate and TAA must read it at the pixel's
   *previous* position (they already compute it -- `c.pastUv`, `historyUV`), not at this frame's texel;
4. the record's "moving" flag reads the velocity lane, which includes camera motion -- use the
   G-buffer's static sign (and object motion) instead;
5. the record must be retaken each frame the camera moves (its cost then, ~0.2 ms, to be measured).
The replay itself (stored point, eye, picks, rays) is already camera-independent.
'''

HEADER_OLD = ('**Read this first.** Updated 2026-09-13 (late night): **the entry below headed "validation\n'
              'cleanup mid-way" is the current hand-off**')
HEADER_NEW = ('**Read this first.** Updated 2026-09-14: **the entry below headed "the anti-lag replaced by\n'
              'measuring change" is the current hand-off**, on top of the 2026-09-13 late-night entry after it,\n'
              'whose uncommitted state still stands.\n\n'
              '**Superseded header.** Updated 2026-09-13 (late night): **the entry below headed "validation\n'
              'cleanup mid-way" is the current hand-off**')
ANCHOR = '## 2026-09-13 (late night): validation cleanup mid-way, and the anti-lag to be replaced by measuring change -- STOPPED HERE\n'

raw = io.open(PATH, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')
for old in (HEADER_OLD, ANCHOR):
    if s.count(old) != 1:
        sys.exit('matched %d: %r' % (s.count(old), old[:60]))
s = s.replace(HEADER_OLD, HEADER_NEW).replace(ANCHOR, ENTRY + '\n' + ANCHOR)
io.open(PATH, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
b = io.open(PATH, 'rb').read()
print('HANDOFF written; CRLF %d, bare LF %d' % (b.count(b'\r\n'), b.count(b'\n') - b.count(b'\r\n')))
