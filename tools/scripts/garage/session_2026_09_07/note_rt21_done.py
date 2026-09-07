"""RT-2.1 done: the record into docs/RT-SERIES.md, the rows, the AO decision,
the hand-off's eleventh entry, NEXT.md's header, the flag's doc line, memory."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')


def load(p):
    s = open(p, 'rb').read().decode('utf-8')
    return s, ('\r\n' if s.count('\r\n') > s.count('\n') / 2 else '\n')


def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)


def rep(s, nl, old, new):
    for ending in (nl, '\n' if nl == '\r\n' else '\r\n'):
        o = old.replace('\n', ending)
        if s.count(o) == 1:
            return s.replace(o, new.replace('\n', ending))
    raise AssertionError('anchor: ' + old[:90])


# --- RT-SERIES.md ------------------------------------------------------------
p = 'docs/RT-SERIES.md'; s, nl = load(p)
assert '### RT-2.1' not in s
s = rep(s, nl, "| measure 0.5 d; the resolve medium |",
        "| measure 0.5 d; the resolve medium — **✅ done 2026-09-07, record below: the measurement, and the fix it pointed at (the parallax march at mip 0, not the raster); the resolve proposed with its number, not built** |")
s = rep(s, nl, "| RT-2.1 | 0.5 d to measure; 2–3 d for the resolve | moderate |",
        "| RT-2.1 | ✅ done in a day (2026-09-07): the measurement and the parallax fix; the resolve is proposed at ~0.6 ms on Headland, gated on the albedo lane's storage | low |")
s = rep(s, nl, "**The picture changes as designed, and it is a look change the owner should judge:**",
        "**The picture changes as designed, and it is a look change the owner should judge (accepted by the owner 2026-09-07):**")
s = rep(s, nl, "the doubled rasterisation of the pending kinds is **RT-2.1** (owner-filed as RT-2's sub-task).",
        "the doubled rasterisation of the pending kinds is **RT-2.1** (owner-filed as RT-2's sub-task; ✅ done 2026-09-07 -- it was the parallax march, not the raster; record below).")
record = """### RT-2.1 — ✅ done 2026-09-07 (uncommitted, both copies staged)

**The framing was wrong, and the measurement said so before anything was built.** RT-2's record blamed "the doubled rasterisation of the pending kinds -- Headland's 4.6 M-triangle terrain". The 4.6 M was the benchmark's whole-frame *triangles submitted* line (shadow maps and the water included); the terrain in the camera's view is **184 chunks and 751 K triangles**, and the benchmark now says so -- a new `terrain:` line: the chunks drawn per level with their triangles, what the distance rule alone wanted, how many chunks the ground's veto and the neighbour cap held finer. An offline replica of `Terrain::SelectLod` (`tools/scripts/garage/session_2026_09_07/terrain_lod.py`: the same error metric, distance rule, veto, cap, skirt rule and frustum test) matches the runtime within 10% and prices any rule change without a rebuild.

**What was ruled out, one measurement each (Headland, 1600x900, `--frame-time=0.0166`, the RT-2 build):**
- *The LOD veto.* `--terrain-lod-error=0.3` (a new measurement flag; the veto off): 751 K → 324 K triangles, the G-buffer pass 3.64 → 3.49 ms, the lit pass 4.36 → 4.19. The veto is worth 0.15 ms a pass and stays (it exists for the ray tracer, which traces level 0 whatever is drawn).
- *The draw count.* Chunks of 128 quads (64 chunks, 50 drawn instead of 184): G-buffer 3.39, lit 4.45 -- nothing; restored to 64.
- *The pixels.* At 400x225, a sixteenth of the pixels, the G-buffer pass was still 1.68 ms and the lit pass 2.24: the cost did not scale with pixels either.
- *The terrain at all.* A scene copy without the TerrainComponent: G-buffer 0.08 ms, lit 0.26, the frame 10.5 ms against 20.3. The terrain was the whole of both passes and half the frame (the traced passes grow with it too, +1.5 ms of DirectTrace, ReflectionTrace and the accumulates: more surface to trace from, legitimate).
- *The parallax march.* The layered material marches each layer's height map 8-24 steps, twice (the floor and the wall projection), `textureLod(…, 0.0)` at every step. With the march disabled in the runtime's shader copy: G-buffer 3.64 → 0.53 ms, lit 4.36 → 2.02, the frame 20.3 → 14.2. **5.4 ms of a 20 ms frame was four terrain layers marching mip 0 at a kilometre, where every fetch of a layer's dozen is a cache miss** -- which is why the cost scaled with neither pixels nor triangles: a sparser pixel grid makes each fetch miss harder.

**The fix: the march reads the height at the pixel's own mip.** `FootprintLod(map, ddx, ddy)` -- the hardware's isotropic level-of-detail rule from the coordinate's explicit derivatives -- goes into `Parallax` (a material) and `ParallaxLayer` (a terrain layer), computed outside divergent control flow (the material's before its branch; the layers' from the per-layer derivatives that were already there). A far pixel now marches the relief its footprint sees, filtered as its colour is; up close the footprint is under a texel and the level is zero, as before. **Measured:** G-buffer 3.64 → 0.70 ms, lit 4.36 → 2.20, **Headland 20.3 → 14.5 ms** (the no-march floor was 14.2); the garage 11.55 → 11.65 ms (noise: its height maps sit at level 0 at that distance). **The picture:** Headland's still against the baseline, mean 0.001 levels, max 0.7, no pixel off by more than 2; the garage's exactly 0.000 (`build/rt21/headland_mip_diff_x8.png`, `garage_mip_diff_x8.png`, made by the session's `diff_still.py`). Raster mode (`--ray-tracing=off`) compiles and runs.

**What is left of "the doubled rasterisation", honestly priced now:** the terrain's raster plus its material is the G-buffer pass's 0.62 ms at Headland (0.70 less the bridge's 0.08), and the lit pass pays the material once more -- about 0.6 ms of its 2.2. The deferred resolve -- the lit pass reading albedo, normal, roughness, metallic, specular and occlusion from the G-buffer for every opaque kind instead of sampling the material again, *the raster kept*, so the tangent, the emissive map, the coat and sheen uniforms stay where they are and no lane is added -- is worth about that 0.6 ms here, more on a scene with heavy materials near the camera. **Not built: it has a precondition for the owner.** The G-buffer's albedo lane is `R8G8B8A8_UNORM` *linear* ("sRGB storage once the attachment path is checked", step 1a's note); a lit pass fed from 8-bit linear albedo bands in the dark tones, so the lane goes to sRGB8 or 16F first, then the variant (`RV_GBUFFER_FED` on the six lit-kind pipelines; three bindings re-committed in `DrawLit` the way 26-28 are; verified by diff image against the sampled path). A small item; the numbers say it is not urgent.

**Noted, no item:** under RT-first the veto's reason (rays trace level 0) could go by the TLAS carrying each chunk's *selected* level, but the veto costs 0.15 ms a pass. Chunks of 128 quads were neutral on the GPU (fewer draws, coarser LOD granularity: 1.24 M triangles for the same view) and stay at 64. The benchmark's *triangles submitted* is the whole frame, shadow maps and water included -- never read it as one pass's count again.

"""
s = rep(s, nl, "## The S series, for the record (owner asked 2026-09-06)", record + "## The S series, for the record (owner asked 2026-09-06)")
save(p, s)

# --- HANDOFF.md --------------------------------------------------------------
p = 'docs/HANDOFF.md'; s, nl = load(p)
assert '2026-09-07' not in s
old = "**Read this first.** Updated 2026-09-06, late night,"
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:i] + ("**Read this first.** Updated 2026-09-07, after RT-2.1: **the eleventh entry below is this session's hand-off**, the tenth (2026-09-06) the complete one for the RT-first state (recipes, flags, traps, what is where). `docs/RT-SERIES.md` is the one list with the records of RT-1, RT-2 and RT-2.1. Nothing is committed. **Next is RT-3, only on the owner's green signal.** The AO look is accepted; the deferred resolve is proposed (RT-2.1's record) and waits on the owner.") + s[j:]
entry = """
## 2026-09-07: RT-2.1 done -- the terrain's cost was the parallax march at mip 0, not the raster -- UNCOMMITTED, halted on the owner's instruction after the report

**Read this, then RT-2.1's record in `docs/RT-SERIES.md`, then the tenth entry below for everything else (its recipes, flags and traps all still hold).** The owner's instructions this session: "AO looks fine, proceed with RT-2.1 first", then "after completing RT-2.1 stop the work and update the hand-off". Both done. Both builds and both staged shader folders are at the RT-2.1 state (`cmp` clean on `include/pbr_fragment.glsl`); no process left running; the test scene copy (`rt21_noterrain.rage` and the `.meta` the engine made for it) deleted from the scenes folder; the chunk size back at 64.

### What RT-2.1 found and did (the numbers are in its record)

- **The measurement first, as filed.** The terrain at Headland is 184 chunks and 751 K triangles, not 4.6 M (that was the whole frame's *triangles submitted*). The LOD veto is worth 0.15 ms a pass; the draw count nothing; the pixel count not it either. A scene without the terrain: G-buffer 0.08 ms, lit 0.26, the frame 10.5 against 20.3. The parallax march of the four terrain layers at mip 0 was 5.4 ms of the frame -- every fetch a cache miss at a kilometre.
- **The fix:** the march reads the height at the pixel's own mip (`FootprintLod`, from explicit derivatives). Headland 20.3 → 14.5 ms (G-buffer 3.64 → 0.70, lit 4.36 → 2.20); the garage unchanged; the stills bit-close (mean 0.001 levels, max 0.7; the garage 0.000). One shader file changed: `include/pbr_fragment.glsl`.
- **The instruments, permanent:** the benchmark's `terrain:` line (chunks drawn per level with triangles; what distance alone wanted; how many the veto and the cap held finer) -- `Terrain::LodReport`, `Renderer3D::TerrainStats`, printed by `FrameProfiler`; `--terrain-lod-error=<ratio>` (a measurement flag for the veto, 0 = the engine's 0.0003); `tools/scripts/garage/session_2026_09_07/terrain_lod.py` (the offline replica of SelectLod, prices a rule change without a rebuild); `diff_still.py` (two stills → mean/p99/max levels, per region, a signed x8 image).
- **The resolve, proposed and not built:** worth ~0.6 ms at Headland now; the raster kept, the material read from the G-buffer; **gated on the albedo lane** (8-bit linear today; sRGB8 or 16F first). The owner decides whether it earns an item.

### Last status and the questions put to the owner

**Last status:** RT-2.1 reported as done; halted on the owner's word ("after completing RT-2.1 stop the work and update the hand-off"); nothing reverted, nothing committed; Headland, the garage and the raster mode run clean.

**The questions, exactly as asked:**
1. **The deferred resolve.** It is now worth about 0.6 ms at Headland (the material sampled a second time by the lit pass), and it needs the G-buffer's albedo lane in sRGB8 or 16F first, or the lit pass bands in the dark tones. **Do you want it built as a small item, and if so, which storage for the lane?**
2. **RT-3 is next on the list** (GI as a signal this frame). **Green signal?**

### Traps paid this session

- **A `//` comment appended inside a `\\`-continued macro line eats the backslash.** The first parallax-off test did that in the runtime's shader copy: 12 compile errors, the layered shader gone, the terrain silently undrawn, and the pass times read as a spectacular win. Every run's line now prints `errors N` (`Shader compilation failed` + `[Vulkan]`) and it must be 0 before a number is believed.
- **Pin `--frame-time` for benchmarks too, not only for stills.** Unpinned, the scene's clock runs on the wall clock and the visible chunk set at frame 60 differs between a 50 FPS and a 100 FPS run (184 against 132 chunks), which looks like resolution-dependent culling and is not.
- **The benchmark's *triangles submitted* is every pass that counts a draw** -- shadow maps, water, G-buffer half and lit half -- never one pass's number.
- **A scene copy in the scenes folder gets a `.meta` made for it on first load;** delete both.
- **Reading a mip in a march is not a look change up close:** the footprint's level is zero there. Measure the picture anyway (the diff images are what settled it), and measure the garage beside the bridge -- the material path and the layered path both changed.

### What changed in code (all uncommitted, on top of the RT-2 state)

`RageVEditor/assets/shaders/include/pbr_fragment.glsl` (`FootprintLod`; `Parallax` and `ParallaxLayer` take the mip; the material's call site computes it before its branch; the two SHADE_LAYER call sites pass the per-layer derivatives'), `RageV/src/RageV/Renderer/Terrain.h/.cpp` (`LodReport`, filled by `SelectLod`; the veto's ratio from the flag), `Renderer3D.h/.cpp` (`TerrainStats`, `CountTerrainChunk`, `ReportTerrainLod`, `GetTerrainStats`; reset with the draw counts), `Scene.cpp` (the terrain draw counts its chunks and reports the terrain's LodReport), `Core/FrameProfiler.cpp` (the `terrain:` benchmark line), `Core/EngineConfig.h/.cpp` (`TerrainLevelError`, `--terrain-lod-error`). Scripts: `tools/scripts/garage/session_2026_09_07/{patch_rt21a,patch_rt21b,patch_rt21c,note_rt21_done}.py` (records; the files on disk are the truth), `terrain_lod.py`, `diff_still.py`, `pbr_fragment.glsl.before_rt21` (the shader before the fix, for a bypass test). Captures and diffs in `build/rt21/`; logs `build/bin/Release/RageVRuntime/rt21_*.log`.
"""
old = "## 2026-09-06, late night: RT-first T1–T5, RT-1, RT-2 built and measured"
assert s.count(old) == 1
s = s.replace(old, entry.replace('\n', nl).lstrip('\r\n') + nl + old, 1)
save(p, s)

# --- NEXT.md -----------------------------------------------------------------
p = 'docs/NEXT.md'; s, nl = load(p)
old = "> **2026-09-06 late night: RT-1 and RT-2 are done"
assert s.count(old) == 1
s = s.replace(old, "> **2026-09-07: RT-2.1 done (records in `docs/RT-SERIES.md`): the terrain's cost was the parallax march at mip 0, not the raster -- Headland 20.3 → 14.5 ms, the picture unchanged; HANDOFF.md's eleventh entry is this session's hand-off. The AO look is accepted. Next is RT-3 on the owner's green signal; the deferred resolve is proposed at ~0.6 ms and gated on the albedo lane's storage.**" + nl + nl + old, 1)
save(p, s)

# --- EngineConfig.h doc line -------------------------------------------------
p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
assert '--terrain-lod-error=R' not in s
s = rep(s, nl, """//                           target). --light-sampling is the old name, still
//                           read.
""", """//                           target). --light-sampling is the old name, still
//                           read.
//   --terrain-lod-error=R    the terrain's LOD veto: the error a chunk may
//                           carry as a fraction of its distance (0 = the
//                           engine's 0.0003). A measurement flag (RT-2.1).
""")
save(p, s)

# --- memory ------------------------------------------------------------------
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_rt_series_state.md'; s, nl = load(p)
s = s.replace("description: RageV RT-first state as of 2026-09-06 late night -- T1-T5, RT-1, RT-2 done and uncommitted; RT-3 next only on the owner's green signal; two decisions waiting on the owner; the traps of this session",
              "description: RageV RT-first state as of 2026-09-07 -- T1-T5, RT-1, RT-2, RT-2.1 done and uncommitted; RT-3 next only on the owner's green signal; the AO look accepted; the deferred resolve proposed and waiting on the owner; the traps of these sessions", 1)
old = "**Next: RT-3 (GI as a signal this frame), only on the owner's green signal, after they\njudge two things RT-2's record puts to them:**"
assert old.replace('\n', nl) in s
s = s.replace(old.replace('\n', nl), ("**2026-09-07: RT-2.1 DONE.** The terrain's cost was never the raster: the four terrain\nlayers' parallax march read mip 0 at a kilometre (every fetch a cache miss) -- 5.4 ms of\nHeadland's 20 ms frame; the march now reads the pixel's own mip (`FootprintLod`), Headland\n20.3 -> 14.5 ms, the stills within 0.001 levels, the garage unchanged. Ruled out by\nmeasurement: the LOD veto (0.15 ms a pass), the draw count, the pixel count. Instruments:\nthe benchmark's `terrain:` line, `--terrain-lod-error`, the offline SelectLod replica\n(`session_2026_09_07/terrain_lod.py`). The deferred resolve is PROPOSED, not built (~0.6 ms\nat Headland; gated on the albedo lane going sRGB8/16F). **The AO look is accepted** (owner,\n2026-09-07). HANDOFF's eleventh entry is this session's hand-off.\n\n**Next: RT-3 (GI as a signal this frame), only on the owner's green signal; the two\nthings RT-2's record put to them were:**").replace('\n', nl), 1)
save(p, s)

p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
old = "- [RT-series state (2026-09-06 late night)](project_ragev_rt_series_state.md) — T1-T5, RT-1, RT-2 done and uncommitted; RT-3 only on the green signal; two decisions wait on the owner (the AO look, the G-buffer's terrain cost); HANDOFF's tenth entry is the full hand-off"
assert s.count(old) == 1
s = s.replace(old, "- [RT-series state (2026-09-07)](project_ragev_rt_series_state.md) — T1-T5, RT-1, RT-2, RT-2.1 done and uncommitted; RT-2.1's lesson: the terrain's cost was the parallax march at mip 0, not the raster (Headland 20.3 → 14.5 ms); AO look accepted; the deferred resolve proposed, gated on the albedo lane; RT-3 only on the green signal; HANDOFF's eleventh entry is the hand-off")
save(p, s)
print('RT-2.1 record done')
