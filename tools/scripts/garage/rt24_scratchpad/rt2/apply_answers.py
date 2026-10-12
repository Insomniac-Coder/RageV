"""Write the owner's twelve answers (2026-09-24) into RT-SERIES-2.md.

All new text uses the numbering BEFORE the final renumber (RT2-10 is removed at
the end and RT2-11..44 shift down by one).
"""
import re

P = 'RT-SERIES-2.md'
t = open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global t
    n = t.count(old)
    assert n == count, (n, old[:120])
    t = t.replace(old, new)


def block(start, end, new):
    """Replace from the line starting with `start` up to (not including) `end`."""
    global t
    a = t.index(start)
    b = t.index(end, a)
    t = t[:a] + new + t[b:]


# ---------------------------------------------------------------- header + short version
rep('*Revised draft for the owner, 2026-09-24. Nothing in it is built or decided.',
    '*Roadmap for the owner, 2026-09-24, with the owner\'s answers to its twelve decisions (section 5). Nothing in it is built yet.')
rep('(RT2-1 to RT2-10)', '(RT2-1 to RT2-9)')
rep('  - a world radiance cache that starts from the bake;\n',
    '  - bounce light in two layers: the bake for baked lights, and a live pass on top for Realtime lights (the owner\'s design, D4);\n')
rep('- **Size.** 45 items, roughly 230 to 295 working sessions', '- **Size.** 44 items, roughly 230 to 295 working sessions')
rep('- **Target.** Proposed: 60 FPS at the laptop\'s 2560x1600, rendered internally at 1920x1200, with about 2 ms kept free because this GPU\'s timings drift. Today\'s garage, scaled to that internal resolution, costs about 29 ms (an estimate). The per-pass targets in section 6 are proposals only: each item\'s first arm measures, and the owner sets the bar from that measurement.',
    '- **Target.** 60 FPS at the laptop\'s native 2560x1600 (the owner\'s choice, D8), with about 2 ms kept free because this GPU\'s timings drift. Today\'s garage at that resolution would cost about 52 ms (an estimate from the 1600x900 benchmark), so it has to get about 3.6 times faster. Render scale stays available as a fallback setting, but the plan does not count on it. The per-pass targets in section 6 are proposals only: each item\'s first arm measures, and the owner sets the bar from that measurement.')
a = t.index('- **Owner decisions.** Section 5 lists twelve')
b = t.index('\n', a)
t = t[:a] + ('- **Owner decisions.** All twelve were answered on 2026-09-24; section 5 has each answer and what it changed. In short: '
             'this series includes the speed pass; OpenGL is frozen; the RT mode may replace its second scene draw with one compute pass; '
             'TAA stops being forced once every signal stands alone, and MSAA 4x becomes selectable; bounce light comes in two layers '
             '(baked lights from the bake, Realtime lights live on top); the engine\'s own upscaler is the default, with DLSS and FSR optional; '
             'shader caches are on; glowing surfaces become lights, judged first; the target is 60 FPS at native resolution; RT-24 may be '
             'committed with its one TAA reliance written down; the accepted sea work returns with the water rework; and the back glass '
             'panes stay a named TAA exception.') + t[b:]

# ---------------------------------------------------------------- glossary
rep('- **Half float (RGBA16F)**:',
    '- **Baked light / Realtime light**: a light whose bounce (and, for Full and Hybrid bakes, its far direct light) is stored in the bake, against one lit entirely live. Only Realtime lights can move or be switched without a re-bake.\n- **Half float (RGBA16F)**:')

# ---------------------------------------------------------------- section 1
rep('  - RT-14 measured the traced passes as 90-99% linear in pixel count, so the biggest lever, fewer pixels, is still untouched.',
    '  - RT-14 measured the traced passes as 90-99% linear in pixel count. Rendering fewer pixels would be the biggest lever, but the owner\'s target is native resolution (D8), so it stays a fallback setting, not part of the plan.')

# ---------------------------------------------------------------- section 2
rep('Proposed: 1920x1200 internal resolution, 2560x1600 output (decision D8).',
    'The owner\'s target (D8): 60 FPS at native 2560x1600. A lower render scale with upscaling stays available as a setting, but nothing below counts on it.')
rep('   - the radiance cache\'s update rays, within their budget.',
    '   - the realtime layer\'s cache updates, only while a Realtime light is near the camera.')
rep('   - GI (half resolution) and AO (half resolution);',
    '   - the realtime bounce layer (half resolution, only while a Realtime light is near the camera) and AO (half resolution);')
rep('5. **Composition** (compute): emissive + direct + indirect × albedo',
    '5. **Composition** (compute): emissive + direct + indirect (the baked layer plus the realtime layer) × albedo')
rep('7. **Final temporal pass.** It upscales to 2560x1600 and anti-aliases geometry only.',
    '7. **Final temporal pass.** It anti-aliases geometry only, at native 2560x1600 (and upscales when a lower render scale is chosen).')
rep('hits lit by the one hit-shading contract, with the radiance cache beyond the first hit',
    'hits lit by the one hit-shading contract, with stored light (the bake, plus the realtime layer\'s cache) beyond the first hit')
rep('| GI | One ray per pixel at half resolution; the first hit shaded, the radiance cache beyond it | The contract on the trace\'s own grid, a relative outlier stage, one joint bilateral upsample | Nothing |',
    '| Bounce light (GI) | Two layers (D4). Baked lights: read from the bake, no rays. Realtime lights: one ray per pixel at half resolution, each hit lit by Realtime lights only, a small cache beyond it; the pass is skipped when no Realtime light is near | The realtime layer: the contract on the trace\'s own grid, a relative outlier stage, one joint bilateral upsample. The baked layer needs none | Nothing |')
rep('The contract on its own grid; applies to light from the cache only',
    'The contract on its own grid; applies to stored bounce light only (the bake and the realtime layer\'s cache)')
rep('WBOIT, lit from the light library and the cache; no random term',
    'WBOIT, lit from the light library and the stored bounce light; no random term')
rep('which breaks "no reliance on TAA": decision D11 asks the owner to accept this as a named exception or to give them a history-free estimator',
    'a named exception to "no reliance on TAA", accepted by the owner (D11)')
rep('(Vulkan; OpenGL per D1)', '(Vulkan only; OpenGL is frozen, D1)', count=2)
rep('| GI | Rewrite into a radiance cache. Keep the RT-3 arrangement, the shared hit shader, the NEE machinery, the bake stamps and the fill solver\'s parts | Four different answers today, and no cache |',
    '| GI (bounce light) | Two layers (D4): the bake for baked lights, kept as it is, plus a live pass for Realtime lights on top. Keep the RT-3 arrangement, the shared hit shader, the NEE machinery, the bake stamps and the fill solver\'s parts | Four different answers today, and Realtime lights never bounce in baked scenes |')
rep('then use them only as a rough-reflection fallback read from the cache', 'then use them only as a rough-reflection fallback')
rep('| TAA | Refactor into an anti-aliasing and upscaling pass for geometry only |',
    '| TAA | Refactor into an anti-aliasing pass for geometry only (upscaling when a lower render scale is chosen) |')
rep('| OpenGL backend | Owner decision D1 (freeze or drop) | |',
    '| OpenGL backend | Freeze (D1): the raster path keeps working as today, with a shader set that stops changing; new features are Vulkan-only | It cannot ray trace, and parity would hold the new design down |')

# ---------------------------------------------------------------- section 3
rep('If it cannot, decision D9 asks the owner whether RT-24 may be committed with it',
    'If it cannot, RT-24 is committed with the reliance written down (D9), and RT2-24 and RT2-29 remove it')
rep('D2 decides the compute step after it', 'Then, per D2, one compute composition replaces the second scene draw in RT mode')
rep('| ISS-9 | The baked probe is a third brighter than the traced reflection | Fixed directly, then absorbed | RT2-6 (probes get shadows; the reference renderer settles which is right), RT2-28 (rough reflections from the cache, not the probe) | |',
    '| ISS-9 | The baked probe is a third brighter than the traced reflection | Fixed directly, then absorbed | RT2-6 (probes get shadows; the reference renderer settles which is right), RT2-28 (rough reflections from stored light, not the probe) | |')
a = t.index('| ISS-10 |')
b = t.index('\n', a)
t = t[:a] + ('| ISS-10 | Switching a baked light throws the bake away | Absorbed | RT2-26 | Switchable lights are Realtime (the owner\'s rule), '
             'and with RT2-26\'s realtime layer they now bounce light too. Switching a baked light still means a re-bake, by design (D4: baked lights stay baked) |') + t[b:]
a = t.index('| ISS-11 |')
b = t.index('\n', a)
t = t[:a] + ('| ISS-11 | The sea work the owner accepted on 2026-09-20 ("looks perfect to me, make it default") is on a parked branch; main\'s sea mirror has no accumulator | '
             'Absorbed | RT2-1 (its validation fixes), RT2-32 and RT2-33 | The owner chose to bring it back with the water rework in M5 (D10). Costs recorded then at 1440p: '
             'the pier +1.7 ms, Glitter +1.2 ms, Headland 1.3 ms faster. It was never checked with a moving camera, so RT2-33 checks that before it becomes the default |') + t[b:]
rep('| Fixed directly | RT2-7, then decision D3 | |', '| Fixed directly | RT2-7; the force is lifted at the end of M5 (D3, RT2-34) | |')
rep('; D11 asks whether that stays as a named exception', '; the owner accepted that as a named exception (D11)')
rep('the sea mirror\'s accumulate to RT2-10 or RT2-33 (D10)', 'the sea mirror\'s accumulate to RT2-33 (D10)')
rep('RT2-9 (decision D6)', 'RT2-9 (D6: approved)')
a = t.index('| ISS-37 |')
b = t.index('\n', a)
t = t[:a] + ('| ISS-37 | Baked-lighting limits the traced picture now depends on | Absorbed | RT2-27 (including contact-scale bleeding and cells that move out of walls, both in its exit gate), '
             'RT2-41 (BC6H in the texture cooker) | Baked lights stay baked (D4), so reflections of Static surfaces keep taking baked lamps\' light from the field, by design. '
             'Nested volumes blend; the light that bleeds at contact scale (about 1.9% of pixels) is measured against the reference renderer and must fall; cells inside walls move out, '
             'which fixes the leak case the author rule could not; a stale bake is flagged in the editor. The bake on OpenGL stays unsupported (OpenGL is frozen, D1). '
             'Lightmaps are not planned (meshes carry one UV set) |') + t[b:]

# ---------------------------------------------------------------- items
rep('If it cannot, decision D9 goes to the owner.',
    'If it cannot, RT-24 is committed with the reliance written down (the owner\'s answer to D9), and RT2-24 and RT2-29 remove it later.')
rep(', or D9 is answered.', ', or the remaining reliance is written down (D9).')
rep('  - Decision D3 (lift the forced TAA, or keep it) is not asked here. Young and moving pixels still lean on TAA through the sea\'s in-draw rays and the back panes until RT2-32, so D3 waits until then.',
    '  - The forced TAA is not lifted here. Young and moving pixels still lean on TAA through the sea\'s in-draw rays until RT2-32 and RT2-33, so the owner\'s answer to D3 is carried out at the end of M5 (RT2-34).')
rep('  - Per decision D6, the SPIR-V cache is switched on', '  - Per D6 (approved), the SPIR-V cache is switched on')
rep('**Depends on.** D6 and RT2-1. It needs none', '**Depends on.** RT2-1 (D6 approved). It needs none')
# remove the conditional sea item
block('### RT2-10 · Re-land the sea work', '### RT2-11 · ', '')
rep('  - Whether an emissive mesh with no light becomes a direct light is decision D7, since RT-22 ruled that it lights nothing by design. It is offered as an arm, not built as the default.',
    '  - Per D7, every glowing surface without a light of its own becomes a light source. It is shown to the owner as its own arm first, and becomes the default only after the owner judges it. A surface that a light already owns (the tube bars) stays counted once.')
rep('- **Owner sees.** Calmer tube reflections, because every bar is aimed at.',
    '- **Owner sees.** Calmer tube reflections, because every bar is aimed at. Glowing objects with no light of their own, like RT-22\'s cube, light what is around them (once the owner has judged that arm).')
rep('; whether only TAA may keep averaging those is decision D11.', '; they stay a named exception to "no reliance on TAA", which the owner accepted (D11).')
rep('which is cheaper to add now than to retrofit in RT2-30 and is why this item needs D8.',
    'which is cheaper to add now than to retrofit in RT2-30 (render scale stays an optional setting under D8\'s native target).')
rep('**Depends on.** D1, D8, RT2-1 to RT2-4, RT2-15.', '**Depends on.** RT2-1 to RT2-4 and RT2-15 (D1 and D8 answered).')
rep('  - scenetest green on the backends D1 keeps.', '  - scenetest green on Vulkan, and on OpenGL\'s frozen raster path.')
rep('**Depends on.** D1, RT2-16, and RT2-18 (which', '**Depends on.** RT2-16 and RT2-18 (which')
rep('GPU-12 and MAT-14 (as D1 decides)', 'GPU-12 and MAT-14 (OpenGL frozen, D1)')
rep('MSAA with rays runs only if D3 lifts the forced TAA.', 'MSAA with rays becomes selectable once the forced TAA is lifted at the end of M5 (D3).')
rep('  - **Step 2** (decision D2) is a full-screen compute composition',
    '  - **Step 2** (approved by the owner for RT mode only, D2, and kept only if it measures faster with a near-identical picture) is a full-screen compute composition')
rep('+ AO on cache light only + fog', '+ AO on stored bounce light only + fog')
rep('**Depends on.** D2, RT2-22.', '**Depends on.** RT2-22 (D2 approved).')

RT2_26 = '''### RT2-26 · Bounce light from Realtime lights, added on top of the bake; one hit-shading contract

- **What is wrong now.**
  - In a baked scene the engine switches the live bounce pass off completely once the bake is ready. So Realtime lights give direct light but no bounce light at all: the garage has 7 of them and the bridge 14. The floor under RT-22's moving cube lights up, but none of that light reaches the car or the walls.
  - A surface is lit four ways depending on who looks at it: the lit pixel; a traced hit, using probe irradiance with no occlusion; a bake hit, using the sky with no occlusion; a probe capture.
  - The live bounce, where it runs, costs 2.8 to 3.0 ms of trace time (+3.6 ms of frame) at 1600x900 when forced on in the garage, and cannot serve the bridge (only the sky as second-bounce light, a 12 m reach).
  - The one world-space light store that exists (a 24 m box that follows the camera) is off in every shipped scene and read by no ray hit; its texture does not move with it, so misplaced light takes seconds to wash out.
  - Fixed clamps tuned on the showroom (4 and 32) cut real energy in brighter scenes, and the accumulator's diffuse kind has no stage for outliers.
- **What changes.** The owner's design (D4): bounce light in two layers that add up. Light adds up, so bounce from the baked lamps plus bounce from the live lamps equals bounce from all of them, and the two layers are exact together as long as the Static scene has not changed since the bake. Four named steps, behind `--realtime-bounce`, each ending in a report:
  - **26a. The layering rule.** Each light's bounce comes from exactly one layer. Half, Full and Hybrid lights: from the bake, read as today (baked lights stay baked). Realtime lights: from the realtime layer. In a scene with no bake (the camp), every light is Realtime, so the realtime layer carries all of it.
  - **26b. The realtime layer.** About one ray per pixel at half resolution. Each hit is lit by Realtime lights only, because the baked lights' bounce is already in the bake and would otherwise count twice. The whole pass is skipped when no Realtime light reaches the view, so a fully baked view pays nothing. A relative outlier stage replaces the fixed clamps, and the history-box width is re-measured on the trace's grid. A small camera-centred cache (cascades, wrap-around addressing, a fixed update budget as a global setting) stores the realtime layer's light so that reflection hits and second bounces can read it; it is built only if measurement shows it pays.
  - **26c. One hit-shading contract**, used by the realtime layer, reflections, the bake and probes: direct light from the candidate structure; bounce light from the bake for baked lights plus the realtime layer's cache for Realtime lights; sky light from occluded rays. The hit specular term (GI-09) is measured here.
  - **26d. Probes captured through the ray-traced world.** A probe is rendered by the same hit-shading contract, so it shows what the rays see. After RT2-28 it serves only as the rough-reflection fallback.
- **Owner sees.** Realtime lights fill the room around them with bounce light, the way baked lamps do. A fully baked view costs nothing extra. The camp's live bounce is cheaper. Probes agree with the traced picture.
- **Touches.** `rtgi_trace.rvshader`, `pbr_fragment.glsl` (`TraceSurface`, `ShadeTraced`), `irradiance_fill.rvshader`, `reflection_trace.rvshader`, `ReflectionProbe.cpp`, `RuntimeIrradianceField.*` (replaced by the small cache, if built), `Renderer3D.cpp`, `Scene.cpp` (the GI source switch; the field, cache and probe ownership).
- **Scope.** Rewrite. **Depends on.** RT2-2, RT2-11, RT2-12, RT2-13, RT2-21, RT2-22. **Risk.** Medium: every light must be counted in exactly one layer, including Hybrid lights, whose near direct light is live while their bounce and far direct light are baked.
- **Exit gate.**
  - `--pass-timings`: the realtime layer absent in a fully baked view, present when a Realtime light is near.
  - The garage with its Realtime lights on: the baked layer plus the realtime layer against the reference renderer with every light: near zero, or favouring baked (the owner's bar).
  - RT-22's moving cube: its bounce follows it (`emitter_lag.py`), and nothing is counted twice where it passes a baked lamp.
  - The camp (all Realtime): distance to the reference renderer within grain.
  - The leak scenes in `check_gi.py` stay at 0.000.
  - The garage probe captured through the traced world, diffed against the reference renderer at the probe's position.
  - A,B,B,A at native resolution. The proposal is about 1 ms for the realtime layer while Realtime lights are near; the owner sets the bar from 26b's first measurement.
  - The cache's VRAM, if it is built.
- **Size.** 9-12 sessions.
- **Closes.** GI-02, GI-05, GI-07, GI-09, GI-11, ISS-10; the Realtime-light half of GI-01; the traced-capture half of GI-04; the hit part of GI-06.

'''
RT2_27 = '''### RT2-27 · One rule for indirect light, and a bake that knows when it is stale

- **What is wrong now.**
  - The lit pixel adds a probe-or-sky term beside the traced bounce, and the two overlap: a local probe's irradiance already contains the room's light. The field's sky fraction is applied only where the bounce gave no answer. AO, which reaches 0.4 to 0.5 m, multiplies the traced bounce and the stored direct light.
  - The bake's stamp cannot see a moved Static object or an edited material, so a stale bake is silently wrong until someone re-bakes.
  - At most 8 volumes can be placed by hand, and the bridge uses all 8.
  - The field stores fully baked lamps' direct light at cell resolution, where AO darkens it (it does not darken the same lamp's live share).
  - Nested volumes do not blend, the rt and ss files are identical, and there are eleven indirect-light systems.
- **What changes.**
  - One composition rule: indirect light = the baked layer + the realtime layer (RT2-26), each light counted once. The separate probe-or-sky term is dropped wherever a layer gives an answer. AO applies to stored bounce light only, never to direct light, stored or live.
  - Baked lights stay baked (D4): their bounce, and the far direct light of Full and Hybrid lamps, keep coming from the bake at almost no cost.
  - The bake's stamp covers Static objects' transforms and materials, and the editor warns when a Static object has changed since the bake; the fix is a re-bake. Dynamic objects are unaffected: they are lit by both layers and never make the bake stale.
  - Nested volumes blend, and there is one file per bake. The stored field becomes sparse bricks that stream with the world (RT2-41), not one resident atlas capped at 8 volumes.
  - Cells that fall inside walls move out (probe relocation), which fixes the leak case the "cells smaller than the thinnest wall" rule could not. Light bleeding at contact scale (about 1.9% of pixels today) is measured against the reference renderer.
  - The raster recipe reads the same format. The reference arms move behind engine flags; RT2-31 deletes them. The settings documentation is rewritten.
- **Owner sees.** The baked and realtime layers agree where they meet. A stale bake is flagged in the editor instead of being silently wrong. AO no longer darkens baked lamps' direct light.
- **Touches.** `pbr_fragment.glsl` (5611-5925), `IrradianceVolume.*`, `BakedLighting.cpp`, `Scene.cpp` (the bake orchestration and its stamp, 3900-4100), `irradiance_fill.rvshader`, `check_gi.py`, the editor's bake panel, `RenderSettings.h`, `PostSettings.h`.
- **Scope.** Refactor. **Depends on.** RT2-26. **Risk.** Medium.
- **Exit gate.**
  - The baked layer plus the realtime layer against the reference renderer, diffed on the garage, the camp, the bridge cameras and the bright-sky scene: near zero, or favouring baked.
  - `check_gi.py` updated for the new rule (its gi_skylit band encodes today's residual) and green.
  - The leak scenes at 0.000, including the case that needed cells to move out of walls.
  - Contact-scale bleeding: the share of pixels off the reference by more than the grain falls from today's 1.9%.
  - Moving a Static object in the editor raises the stale-bake warning; moving a dynamic one does not.
  - A,B,B,A. Bake time and disk size recorded.
- **Size.** 7-9 sessions, in named steps: the composition rule; the stamp and the editor warning; nested blending, relocation and sparse bricks; the raster recipe and the docs.
- **Closes.** GI-01, GI-03, GI-12 (sparse bricks included), GI-13, GI-14, ISS-37; the field-lookup part of ISS-16.

'''
block('### RT2-26 · A world radiance cache', '### RT2-28 · ', RT2_26 + RT2_27)
rep('### RT2-28 · Reflections tiered by roughness; rough reflections from the cache',
    '### RT2-28 · Reflections tiered by roughness; rough reflections from stored light')
rep('    - rough tiles read the radiance cache\'s directional light instead of the baked probe.',
    '    - rough tiles read the stored bounce light\'s direction (the bake, plus the realtime layer\'s cache) instead of the baked probe.')
rep('Section 6\'s largest single saving. Rough surfaces reflect moving objects and changed lights. No brightness seam across roughness.',
    'Section 6\'s largest single saving. Rough surfaces stop taking their reflection from a probe that is a third too bright. No brightness seam across roughness.')
rep('whatever TAA dependency of fix 1 was left (if D9 accepted one).',
    'the TAA reliance of fix 1 that D9 allowed RT-24 to be committed with, if arm H did not remove it.')
rep('### RT2-30 · Internal render resolution and temporal upscaling', '### RT2-30 · Render scale and upscaling, as an option')
rep('  - The final temporal pass becomes an upscaler: the engine\'s own TAAU first; per decision D5, DLSS Super Resolution and FSR behind the same interface.',
    '  - The final temporal pass can upscale. Per D5, the engine\'s own TAAU is the default, and DLSS Super Resolution and FSR are optional behind the same setting, with their SDKs inside private files. DLSS Ray Reconstruction is only a measured arm.')
rep('  - Its interaction with MSAA is measured, and decision D3 settles where MSAA 4x (the owner\'s deliberate setting) fits once a render scale with TAAU exists.',
    '  - Per D3, MSAA 4x stays selectable at full resolution; a render scale below 1 uses the TAA upscaler.')
rep('- **Owner sees.** The largest frame-time lever: at 0.75 per axis, every per-pixel pass handles about 44% fewer pixels.',
    '- **Owner sees.** A fallback lever, not part of the plan, because the owner\'s target is native resolution (D8). At a render scale of 0.75 per axis, every per-pixel pass handles about 44% fewer pixels, for a softer image the owner judges.')
rep('**Depends on.** D5, RT2-29.', '**Depends on.** RT2-29 (D5 answered).')
rep('Their in-draw reflection rays are the one named TAA exception left (D11), unless the owner chose a history-free estimator for them.',
    'Their in-draw reflection rays are the one named TAA exception, which the owner accepted (D11).')
rep('the mirror half of ISS-11 (if D10 chose "later")', 'the mirror half of ISS-11 (D10)')
rep('  - The sea work the owner accepted on 2026-09-20 is on the parked branch, unless D10 brought it back in RT2-10.',
    '  - The sea work the owner accepted on 2026-09-20 is on the parked branch; per D10 it comes back here.')
rep('  - If D10 chose "later", the parked branch\'s sea work is re-landed here where it maps, as the starting point, and judged live.',
    '  - Per D10, the parked branch\'s sea work is re-landed here where it maps, as the starting point. It is checked with a moving camera (it never was) and judged live by the owner.')
RT2_34 = '''### RT2-34 · Fog, glows and particles in the composition; TAA no longer forced

- **What is wrong now.**
  - Fog is one post pass after TAA over the opaque depth. So traced reflections and refractions carry no fog along their own path: a far tower stays sharp in the near water. See-through pixels are fogged by what lies behind them.
  - Particles are unlit and have no motion.
  - With rays on, the engine still forces TAA.
- **What changes.**
  - Analytic height-fog transmittance and in-scatter at ray hits, along reflection and refraction segments.
  - Fog applied in the composition, before the final temporal pass, to opaque and transmissive surfaces alike.
  - Particles lit from the stored bounce light, plus a direct candidate at each particle's centre.
  - The froxel volume (WR-11) stays in the WR series.
  - **The last step of M5: TAA stops being forced (D3).** With the sea and the nearest glass on their own accumulators, every signal cleans itself up without TAA; the back panes are the owner's named exception (D11). TAA becomes a choice like any other AA mode. With rays it stays the default, and MSAA 4x becomes selectable at full resolution.
- **Owner sees.** The far bridge fogged in its reflection on the sea as well. Lit particles in the ray-traced scene. MSAA 4x available with rays on.
- **Touches.** `fog.rvshader`, the composition shader, the hit shading, `particle*.rvshader`, `ParticleRenderer.cpp`, `FrameGraphBuilder.cpp` (the TAA force), the render settings panel.
- **Scope.** Refactor. **Depends on.** RT2-23, RT2-26, RT2-32, RT2-33. **Risk.** Low.
- **Exit gate.**
  - With fog off, frames byte-identical. A Headland diff judged by the owner's eye. A,B,B,A.
  - The TAA step: with rays under `--aa=none` and under MSAA 4x, every signal converges, moving and young states included (RT2-7's checks); the owner judges MSAA 4x with rays live.
- **Size.** 3-4 sessions.
- **Closes.** MAT-13, the lighting half of MAT-16, the structural half of materials-s2; the prerequisites of ISS-29; ISS-12's forced TAA (D3).

'''
block('### RT2-34 · Fog, glows and particles in the composition', '### RT2-35 · ', RT2_34)
rep('World positions are 32-bit floats everywhere: in the vertex transforms, the TLAS instances, the radiance-cache cells and the histories.',
    'World positions are 32-bit floats everywhere: in the vertex transforms, the TLAS instances, the light-cache cells and the histories.')
rep('the ray-tracing world module, the radiance cache, a fixture scene.', 'the ray-tracing world module, the light cache, a fixture scene.')

# ---------------------------------------------------------------- section 5
rep('| M1 Correct inputs and early wins | RT2-4 to RT2-10 | 15-20 (plus 2-3 if D10 says "now") | Ids exact and stable; samples move in every AA mode; probes shadowed; no races between frames; the bridge water regression fixed; shader and pipeline caches; small CPU wins; (the accepted sea work back, if D10 says so) |',
    '| M1 Correct inputs and early wins | RT2-4 to RT2-9 | 15-20 | Ids exact and stable; samples move in every AA mode; probes shadowed; no races between frames; the bridge water regression fixed; shader and pipeline caches; small CPU wins |')
rep('a world radiance cache that starts from the bake; probes captured through the traced world',
    'bounce light in two layers (the bake, plus a live pass for Realtime lights); probes captured through the traced world')
rep('`--gi-cache`', '`--realtime-bounce`')
rep('| M5 Glass and water | RT2-32 to RT2-34 | 18-23 | Glass and water in the ray-traced world; one transmissive layer; water as a material; fog in the composition |',
    '| M5 Glass and water | RT2-32 to RT2-34 | 19-24 | Glass and water in the ray-traced world; one transmissive layer; water as a material with the accepted sea work; fog in the composition; TAA no longer forced |')

DECISIONS = '''**The owner's decisions, answered on 2026-09-24.** Each answer is written into the items it touches.

| Id | Question | The owner's answer | What it changes |
|---|---|---|---|
| D0 | Scope of this series | RT-series 1 closes once RT-24 is committed; RT-22's remaining parts and RT-2.2 move here; this series also counts as the speed pass planned for after the RT series | Speed work happens inside this series, measured at every step; there is no separate pass afterwards |
| D1 | OpenGL's future | Freeze it | OpenGL keeps running the raster path as today, with a shader set that stops changing; the core RHI becomes Vulkan-shaped and new features are Vulkan-only (RT2-16, RT2-19, RT2-37) |
| D2 | Deferred composition in RT mode (reopened RT-FIRST §4, answer 1) | Allow step 2, RT mode only | RT-2.2 first, then one compute composition replaces the second scene draw in RT mode, kept only if it measures faster with a near-identical picture; materials are not rewritten and the raster mode stays forward+ (RT2-23) |
| D3 | TAA and MSAA once rays no longer need TAA | Lift the force; TAA stays the default | After the glass-and-water work, TAA becomes a choice like any other AA mode; with rays it stays the default, and MSAA 4x becomes selectable at full resolution (RT2-34) |
| D4 | How bounce light works | Two layers, the owner's design: a baked pass, with a realtime pass on top that also adds bounce light from Realtime lights | Baked lights keep the bake exactly as today ("baked means baked" stands for them); Realtime lights get live bounce light, in a pass that runs only while one is near the camera; the editor flags a stale bake when a Static object changes (RT2-26, RT2-27) |
| D5 | Upscaler and denoiser vendors | The engine's own upscaler, with DLSS and FSR optional | Own TAAU by default; DLSS SR and FSR behind the same setting, their SDKs inside private files; DLSS Ray Reconstruction only as a measured arm (RT2-30) |
| D6 | Shader and pipeline caches | Caches on | The SPIR-V cache keyed on the compiler version and build options, plus a persistent pipeline cache; every measurement runs with `--shader-cache=off` (RT2-9) |
| D7 | A glowing object with no light of its own | Yes, judged first | Every glowing surface without a light becomes a light source, shown as its own arm before it becomes the default; a surface a light owns stays counted once (RT2-12) |
| D8 | Frame-rate target | 60 FPS at native 2560x1600 | No upscaling is counted on; the budget in section 6 is re-proposed at native, where the garage must get about 3.6 times faster; render scale stays a fallback setting (RT2-30) |
| D9 | Committing RT-24 while fix 1 leans on TAA | Commit it, with the reliance written down | If arm H cannot remove the reliance, RT-24 is committed anyway and the reliance is removed by RT2-24 and RT2-29 (RT2-0) |
| D10 | The sea work accepted on 2026-09-20 | Wait for the water rework | The early re-land item is dropped; the sea work returns in RT2-33 as the starting point, checked with a moving camera and judged live |
| D11 | The glass panes behind the nearest | A named exception | Their in-draw reflection rays stay on the old path (the owner's RT-13 decision) and are the one accepted TAA reliance (RT2-14, RT2-32) |

'''
block('**Decisions the owner is asked for.**', '## 6. Performance budget', DECISIONS)

# ---------------------------------------------------------------- section 6 (native)
SEC6 = '''## 6. Performance budget

**Baseline** (measured this week at 1600x900, 1.44 megapixels, rays on, the project's settings; section 1 has the table).

**Target** (the owner's choice, D8): 60 FPS at native 2560x1600 (4.10 megapixels), with no upscaling counted on. That leaves a GPU working budget of about 14.5 ms, with about 2 ms kept free: this laptop drifts about 1 ms between back-to-back runs, and the camp rose from 7.19 to 7.96 ms over five minutes on the same build.

**Every per-pass target below is a proposal, not a measured basis.** Nothing in RageV has yet measured, for example, how much of the garage is glossy per tile, which is what the reflection target depends on. So each item's first arm measures, and the owner sets that item's bar from the measurement. The targets show the shape of the budget, and where the time has to come from.

**Scaling assumption.** Native 2560x1600 has 2.84 times the benchmark's pixels, and RT-14 measured the traced passes as 90-99% linear in pixel count. So "today, at native" below is the measured number × 2.84. That column is inferred, not measured. At native the garage has to get about 3.6 times faster overall, and its reflections about 7 times. If measurement shows that cannot be reached at full quality, the fallbacks are the render-scale setting (RT2-30) and per-signal tiers, and the owner sees each trade.

**Garage** (GPU ms):

| Pass group | Measured, 1600x900 | Today at native (inferred) | Proposed target at native | Items that buy it |
|---|---|---|---|---|
| Reflections, opaque (trace 3.82, resolve 3.91, accumulate 0.94, three blurs 1.97) | 10.64 | 30.2 | 4.0 | RT2-24, RT2-28, RT2-21 |
| Direct light (DirectTrace 3.18, accumulate 0.43) | 3.61 | 10.3 | 3.2 | RT2-13, RT2-21, RT2-25 |
| Opaque lit pass, becoming the composition | 1.29 | 3.7 | 1.0 | RT2-23 |
| Glass layer (eight passes) | 0.83 | 2.4 | 0.5 | RT2-32 |
| TAA (native, no upscale) | 0.40 | 1.1 | 0.8 | RT2-29 |
| Transparent | 0.39 | 1.1 | 0.4 | RT2-32, RT2-34 |
| G-buffer (plus classification) | 0.26 | 0.7 | 1.1 | RT2-22 (more lanes), RT2-21 |
| AO | 0.21 | 0.6 | 0.4 | RT2-21 |
| Bounce light, baked layer | 0 (read inside the lit pass) | 0 | 0.3 | RT2-27 |
| Bounce light, realtime layer (7 Realtime lights; runs only while one is near) | 0 (off in baked scenes today) | 0 | 1.0 | RT2-26 |
| Ray-world upkeep and light-structure build (visible time) | not split out | — | 0.5 | RT2-13, RT2-36 |
| Post, UI, change maps, budget chain, the rest | ~0.6 | ~1.7 | 1.2 | RT2-16 (skipping idle passes), RT2-34 |
| **Total** | **18.25** | **~52** | **14.4 with the realtime layer running / 13.4 without** | |

**Bridge, Headland** (GPU ms):

| Pass group | Measured | Today at native (inferred) | Proposed target | Items |
|---|---|---|---|---|
| The sea (Transparent 5.58, DirectWaterShade 1.09, DirectWaterChoose 0.58, WaterSurface 0.75, WaterFoam 0.22, WaterAccumulateLamps 0.17) | 8.39 | 23.8 | 4.0 | RT2-32, RT2-33 (including skipping opaque work under the sea) |
| Opaque lit pass, becoming the composition | 2.28 | 6.5 | 1.2 | RT2-23, RT2-33 |
| Reflections, opaque | ~2.95 | 8.4 | 2.0 | RT2-24, RT2-28 |
| Direct light | 1.14 | 3.2 | 2.0 | RT2-13 (67-118 lights a pixel today) |
| G-buffer | 0.74 | 2.1 | 1.3 | RT2-22, RT2-39 |
| TAA (native) | 0.36 | 1.0 | 0.8 | RT2-29 |
| Bounce light, baked layer | 0 | 0 | 0.3 | RT2-27 |
| Bounce light, realtime layer (14 Realtime lights) | 0 | 0 | 1.0 | RT2-26 |
| AO, fog, glow, post, the rest | ~1.26 | ~3.6 | 1.4 | RT2-34 |
| Ray-world upkeep and light structure | — | — | 0.5 | RT2-13, RT2-36 |
| **Total** | **17.14** | **~49** | **14.5 with the realtime layer / 13.5 without** | |

**Camp.** 7.60 ms measured, about 21.6 ms at native (inferred). Its four lights are all Realtime and it has no bake, so its bounce light is entirely the realtime layer: that goes from about 5 ms at native (trace 1.22, record 0.22, re-light 0.20 and accumulate at 1600x900) to a proposed 1.5, and its reflections from about 7 ms at native to a proposed 1.5. The same items bring it under the budget.

**CPU** (the scale scene, 1280x720; the resolution does not change these):

'''
a = t.index('## 6. Performance budget')
b = t.index('| Case | Today (measured) | Target | Items |', a)
t = t[:a] + SEC6 + t[b:]
rep('**VRAM.** Histories come to about 440 bytes per pixel by format arithmetic: about 1.0 GB at 1920x1200 and 1.8 GB at 2560x1600. Target: at most 150 bytes per pixel at the internal resolution (about 0.35 GB),',
    '**VRAM.** Histories come to about 440 bytes per pixel by format arithmetic: about 1.8 GB at native 2560x1600. Target: at most 150 bytes per pixel (about 0.6 GB at native),')
rep('| RT2-13 | Direct light flat in light count; the bridge\'s direct light at most 1.5 ms | Target; the many-light scene decides |',
    '| RT2-13 | Direct light flat in light count; the bridge\'s direct light about 2 ms at native | Target; the many-light scene decides |')
rep('| RT2-23 | At least 0.6 ms at Headland at 1600x900 (the material paid twice); the composition from 2.1-3.6 ms scaled to 0.7-0.9 | 0.6 measured (RT-SERIES.md:1692) |',
    '| RT2-23 | At least 0.6 ms at Headland at 1600x900 (the material paid twice); the composition from 3.7-6.5 ms at native to about 1 ms | 0.6 measured (RT-SERIES.md:1692) |')
rep('| RT2-24 with RT2-28 | Garage reflections from 17.0 to 3.5 ms scaled; Headland from 4.7 to 1.5 | Target |',
    '| RT2-24 with RT2-28 | Garage reflections from about 30 ms at native to 4; Headland from 8.4 to 2 | Target |')
rep('| RT2-26 with RT2-27 | Camp GI from 3.0 to 1.5 ms; adds about 1.8 ms in baked scenes if D4 goes live | Target |',
    '| RT2-26 with RT2-27 | The realtime layer about 1 ms at native while Realtime lights are near, nothing otherwise; the baked layer about 0.3 ms; the camp\'s all-realtime bounce from about 5 ms at native to 1.5 | Target |')
rep('| RT2-30 | Every per-pixel pass × 0.56 against native 2560x1600 (the tables above already assume it); the upscale adds about 1.0 ms at output resolution | RT-14\'s linearity is measured; the upscale cost is estimated |',
    '| RT2-30 | Not counted: render scale is a fallback under D8\'s native target. At 0.75 per axis, every per-pixel pass handles about 44% fewer pixels | RT-14\'s linearity is measured |')
rep('| RT2-32 with RT2-33 | The sea at Headland from 13.4 to 4.0 ms scaled; garage glass from 1.3 to 0.4 | Target |',
    '| RT2-32 with RT2-33 | The sea at Headland from about 24 ms at native to 4; garage glass from 2.4 to 0.5 | Target |')

# ---------------------------------------------------------------- appendix
rep('| GPU-08 | P | RT2-9 | D6 |', '| GPU-08 | P | RT2-9 | Caches on (D6) |')
rep('| GPU-12 | C | D1, RT2-19 | |', '| GPU-12 | C | RT2-19 | OpenGL frozen (D1) |')
rep('| MAT-14 | C | D1, RT2-19 | |', '| MAT-14 | C | RT2-19 | OpenGL frozen (D1) |')
rep('| direct-03 | P | RT2-12 | D7 for glowing objects with no light |', '| direct-03 | P | RT2-12 | Glowing objects with no light become lights, judged first (D7) |')
rep('| GI-01 | P | RT2-27 | D4 |', '| GI-01 | P | RT2-26, RT2-27 | Two layers (D4) |')
rep('| MAT-06 | P | RT2-7, RT2-10, RT2-32 | The sea mirror early if D10 says now; the back panes are D11 |',
    '| MAT-06 | P | RT2-7, RT2-32, RT2-33 | The sea mirror with the water rework (D10); the back panes are a named exception (D11) |')
rep('| CORE-01 | P | RT2-37 | Decision D1 on OpenGL comes first |', '| CORE-01 | P | RT2-37 | OpenGL frozen (D1) |')

# ---------------------------------------------------------------- section 9
t = t.rstrip('\n') + '''

**After the owner's answers (2026-09-24).** The twelve answers were written into their items. Three reshaped the plan. The item that would have re-landed the sea work early was dropped, because the sea work now returns with the water rework (D10); the items after it were renumbered. Bounce light was redesigned around the owner's two-layer idea (D4): RT2-26 keeps baked lights on the bake and adds a live pass for Realtime lights on top, and RT2-27 makes a stale bake visible in the editor. The target moved to native 2560x1600 (D8), so section 6 was re-proposed at native and render scale became a fallback (RT2-30). Lifting the forced TAA became the last step of M5 (D3, RT2-34).
'''

# ---------------------------------------------------------------- renumber: drop 10, shift 11..44 down
assert not re.search(r'RT2-10\b', t), [m.start() for m in re.finditer(r'RT2-10\b', t)][:5]


def shift(m):
    n = int(m.group(1))
    return 'RT2-%d' % (n - 1 if n > 10 else n)


t = re.sub(r'RT2-(\d+)', shift, t)
open(P, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
