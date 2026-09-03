# Rendering Revamp — the night-realism plan

**Written 2026-09-01, the deliverable of the realism research session** (NEXT.md's
⭐ FIRST row). Research base: four parallel literature strands (case studies,
atmosphere/sky, lights/materials, display/perception) over GDC/SIGGRAPH primary
sources, the Golden Gate's real lighting design (DOE report PNNL-21894, the
bridge district's own records), reference night photographs, five fresh captures
of the current night scene, and a ReSTIR deep-dive. Every work item below has at
least one named shipped precedent and a cost.

**How to use this file.** Each `WR-n` is a self-contained brief for one agent
session. Read `docs/HANDOFF.md` "Start here" and this file's **Ground rules**
before any item. Items say *goal → evidence → exact changes → parameter values →
verification → traps → revert*. File references are given as grep anchors
(symbol names, comment fragments), not line numbers — line numbers rot.
Items marked **[OWNER GATE]** end with a visual judgement only the owner can
make: boot the scene, let them look, one change at a time.

---

## 0 · The target, chosen deliberately: render the photograph

The owner's references are **long-exposure night photographs** (1–30 s tripod
shots). That choice settles otherwise-endless arguments; write-offs follow
from it:

- Fixed exposure (already shipped in the night profile) — keep. No auto.
- **No Purkinje/blue desaturation by default** — cameras have no rods. The
  operator ships behind a switch (WR-12) so the choice stays demonstrable.
- Water reads **smooth**: Beckmann roughness biased calm, streak widening
  emulates the exposure-time integration of glitter — that widening is a
  legitimate model of the photograph, not a hack.
- Lamp cores **clip into bloom/glare** — the photograph's language for
  brightness. Glare structure, not lifted midtones, encodes intensity.
- Saturated photopic colour everywhere; the sky gradient at photographic
  strength, but stars **sparse and dim** — at city-adjacent exposure the
  arithmetic says only a handful survive (urban limiting magnitude ~2–3);
  a dense star field would be neither photograph nor retina.
- No blue light sources exist on the bridge. Blue enters only as sky colour.
  Real moonlight is ~4100 K — *warmer* than daylight; movie-blue night is a
  film convention. The night look is warm sodium **against** cool dark sky,
  by contrast, never by blue fill.
- **Judged on SDR, and written down.** Every display-dependent night constant
  (toe strength, glare band weights, flare-sprite intensity, dither
  amplitude) goes through the night post profile so an HDR parameter set can
  exist later without re-tuning (Frostbite "grade once, output many"; GT7
  makes glare narrower on HDR — a different set, same architecture).
- **DOF effectively off at night** (deep focus / infinity). The references
  are f/8–11 long exposures; "cinematic bokeh" would diff badly against
  every one of them. The night profile currently sets FocalLength 85 /
  Aperture 2.8 / FocusDistance 320 — revisit against this rule.
- Car light trails (red/white ribbons on the deck) define many references;
  if the demo ever gets traffic, authored emissive spline ribbons are the
  photograph-target answer (~a day), not motion-blurred cars.

## 0.1 · The judged checklist (acceptance for the whole programme)

Distilled from case studies + the bridge's actual lighting design + failure
modes. A finished night frame satisfies:

1. All bridge roadway light is HPS-amber (~2000–2100 K, CRI ~22); colour
   visibly collapses under it.
2. Tower floodlighting is graduated (150/250/400 W ratio uplights); tower tops
   **fade into darkness** — an evenly-lit tower instantly reads as a render.
3. Red flashing beacons on tower tops; midspan columns of three-white-over-
   green on both faces; red lights on the south fender. These are the only
   non-amber sources, and they bloom.
4. Sky is azimuthally asymmetric: warm glow dome over San Francisco (south),
   near-dark over Marin (north), non-linear horizon falloff; stars suppressed
   under the glow. The environment probe is rebuilt from this sky.
5. Water is never pure black — it mirrors the sky gradient between streaks.
6. Lamp-row streaks are long continuous amber columns, with a few short
   coloured ones from the navigation lights.
7. Fog sits below/through the deck and is **brighter near lamps** (in-scatter),
   halos every source, swallows the tower tops.
8. Exposure fixed; darks descend smoothly to near-black; nothing floor-lifted.
9. Bloom is thresholdless, low-weight, firefly-suppressed — it differentiates
   sources from surfaces instead of flattening them.
10. No banding in sky/fog/water gradients (TPDF dither at every write).
11. No blue light sources; blue exists only as sky colour.
12. Silhouettes (cables, upper towers) soften with distance and haze.

Judge by the project's standing rule: **per-pixel diff images and the owner's
eye against the reference photographs — never mean levels.**

## 0.2 · Where the frame stands today (captured 2026-09-01, bake-matched)

From five 2560×1600 captures (glitter/headland/deck/pier/cliff cameras):

- The deck camera already genuinely reads night: sodium chain with halos, gold
  kerb pools. Best frame by far.
- Towers read **saturated self-luminous red**, one value top to bottom
  (checklist 2 fails). Railings/suspenders glow red everywhere — the
  `GiIntensity 2.6` lift making red albedo emit; the over-brightening failure
  mode, live in one dial.
- Sky is a featureless linear amber→brown wash with a hard horizon line
  (checklist 4, 12 fail). No city, no glow dome, no stars, no beacons.
- Zero atmosphere: far tower/shore crisp at 1.6 km (checklist 7, 12 fail).
- Water: marine-light streak exists but is **made of discrete sparkle dots** —
  mechanism confirmed (see WR-6); open water carries firefly noise
  (checklist 5, 6 partial-fail).
- Terrain silhouette shows polygonal sawtooth against the sky (LOD row in
  NEXT.md, not this plan's scope).

---

## 1 · Ground rules for every work item

**Build & verify:**
- Shaders live in `RageVEditor/assets/shaders/` (`include/` for shared GLSL).
  `pbr_fragment.glsl` is included into several `.rvshader` files; **the scene
  UBO block is mirrored by hand in `scene_block.glsl` AND `pbr_fragment.glsl`**
  — update both, append-only (shorter prefixes in `debug`/`quad`/
  `particle_vertex` are fine).
- Both backends must compile and render: Vulkan **and** OpenGL. OpenGL has a
  32-sampler limit (`CheckSamplerBudget` in scenetest guards it). `discard`
  needs SPIR-V ≤1.5 to cross-compile.
- `scenetest` must stay green on both backends before any merge.
- Run the runtime **from its own directory**
  (`build/bin/Release/RageVRuntime/`), always `--rhi=vulkan` for anything RT
  or bake-related (the staged `ragev.ini` says opengl). Screenshots:
  `--screenshot=<abs path> --screenshot-frame=N --frame-time=0.016666` and a
  stated `--render-defaults=off` (`=on` silently disables RTAO and skinned
  animation).
- Judge render changes by per-pixel diff images (see any `check_*.py` in
  `tools/scripts/` for the pattern), never by mean levels. For flicker work
  the metric is the **blinking-pixel count**, not the worst pixel.
- Never leave test state behind. One visual change at a time when the owner
  is judging.

**Engine traps that will bite these items specifically:**
- `BeginScene` zeroes the scene block (`s_Data->Scene = {}` in
  `Renderer3D.cpp`) — anything written before it vanishes silently.
- Set 0's bindings are all spoken for; each pipeline family allocates its own
  set against its own layout. New per-frame tables go in the **scene UBO**.
- `InstanceData` is exactly 256 bytes under a `static_assert`; every
  `Indices` lane is taken.
- A set is allocated against a pipeline: identical layout ≠ interchangeable
  (OpenGL resolves against the program; reusing a sibling's set draws black).
- Push-constant vec4s are 16-byte aligned; scalars get padded by the shader
  and not the struct — fields after them read garbage.
- The showroom/scene post profiles are CRLF with no trailing newline —
  appending a key concatenates onto the last line and is silently ignored.
- Stale artefacts: Release-only crashes are usually a stale `Sample.dll`, a
  stale shader cache, or a stale bake — read `docs/HANDOFF.md`'s stale-
  artefact section before debugging one.
- **No visual parameter may be a function of a value that moves frame to
  frame** (the AO-blur lesson). Any temporal/adaptive term needs hysteresis
  and a dead band.
- **The frame may guess; a store may not.** Nothing transient (fallbacks,
  boosts) may be blended into a persisted history or bake.
- **Shared temporal-systems rule (applies to TAA, the froxel volume, and GI
  history alike): flashing or fast-moving emitters bypass or hard-clamp
  EVERY history buffer.** The tower beacons and nav flashers will otherwise
  smear ("lighting trails" — UE documents the artefact) or flicker against
  neighbourhood clamps.
- **Analytic overlays composite AFTER the temporal resolve.** Sprite flares
  (WR-5) are stable by construction; putting them through TAA both dims them
  (tonemapped-weighted resolve suppresses sub-pixel brights) and pollutes
  history. Related: TAA's dimming of stochastic water sparkle is *aligned*
  with the long-exposure target — smooth low-variance streaks are the goal,
  so do not fight the resolve to preserve sparkle.
- A bake is real only if its file has ~118k non-zero bytes of 157k; an
  OpenGL "bake" writes zeros and later runs trust it.
- **A fixed additive dither (or any display-referred constant) is only the
  right size at the write nearest the final low-precision target.** WR-2
  found this the hard way: `1/255` added in sky/fog's linear pre-exposure
  space came out near 10 levels of 255 after `Exposure`/ACES/gamma at a dark
  night value — gamma's encoding slope is steepest near black, which a night
  scene lives in. One dither point after every amplifying stage (this
  engine's tonemap pass) covers every upstream write; dithering upstream of
  exposure/tonemap needs no flat constant, it needs the *local* downstream
  gain, which is not worth computing. Applies to WR-11 and WR-12, both of
  which add HDR-stage writes.

**The scene file is currently hand-owned — do not regenerate it (until WR-0
lands).** Commit `dab692d` added `SourceRadius`/`Range`/`OuterCone` to 120
lamps and 4 marine lights *directly* in `GoldenGateDemo.rage`; the generator
does not know them. Running `make_bridge_scene.py` today silently reverts that
lighting, drops the marine lights, changes the lighting hash, and every run
falls back to Realtime GI with only a log-line warning.

**Camera selection for captures:** lowest `ViewRank` wins, ties break on
entity id, and the loader clamps ranks to [0,99] (a stored −1 becomes 0 — the
committed scene's Glitter camera wins its tie with Headland this way). To
capture a specific camera, set it to 0 and every other camera to 50.

---

## 2 · Work item index

Backend tags: **BOTH** = plain shader/compute, must be identical on Vulkan
and OpenGL. **VK-only** = rides the RT path, which is already Vulkan-only —
OpenGL keeps its current rendering, no fallback owed. **GL-fallback** = the
feature is wanted on both backends and the GL path needs its own mechanism
(named in the item).

| Item | What | Backend | GPU budget (label) | Effort | Depends on |
|---|---|---|---|---|---|
| WR-0 | Generator parity for the hand-edited scene | n/a | 0 | 0.5 d | — |
| WR-1 | Night sky: shaped gradient + city glow + moon | BOTH | ~0 | 2–3 d | — |
| WR-2 | TPDF dither at sky/fog/tonemap writes | BOTH | ~0 | 0.5 d | — |
| WR-3 | ~~Fog inscatter takes the sky's colour per view direction~~ ✅ | BOTH | ~0 (one cube fetch, uniform-branched off) | done | WR-1 |
| WR-4 | ~~Photometry & fixtures pass (scene data)~~ ✅ owner gate | BOTH | +2.3 ms measured (58 more lights, none casting) | done | WR-0 |
| WR-5 | Night glare: ~~lamp flare sprites~~ 🔨 **built** (owner-picked intensity); thresholdless audit and PSF weights still open | BOTH | <0.5 ms (inferred — run the overdraw benchmark in the item) | 2–3 d | WR-4 helps |
| WR-6 | Water sparkle: local specular bound replaces flat ReflectionFloor | VK-only | ~0 | 1 d | — |
| WR-7 | Capsule (source-length) specular — interim streak fix | BOTH | ~0 | 1 d | — |
| WR-8 | LTC area lights: rect, then line/tube for the lamp rows | BOTH | ~3–5× punctual ALU per light, cluster-bounded (measured class: 0.56 ms/full-screen light on a 2014 laptop GPU, near-linear) | 4–6 d | WR-13 with it |
| WR-9 | Luminaire binding + the single-counting contract | BOTH (flags), VK-only (ray-hit tags) | 0 | 1–2 d | WR-8 |
| WR-10 | World-space light grid + stochastic sampling at RT hits | VK-only | net ≤0, build <0.5 ms | 3–4 d | — |
| WR-11 | Froxel volumetrics (or analytic airlight fallback) | GL-fallback (see item) | 1–1.5 ms (shipped-precedent class) | 1–2 wk | WR-1, WR-4 |
| WR-12 | Display finishers: night toe, Purkinje switch, local exposure, stars | BOTH | <0.5 ms | 2–3 d | WR-1, WR-5 |
| WR-13 | ~~Specular antialiasing (Tokuyoshi–Kaplanyan)~~ ✅ **judged good by the owner 2026-09-02**; the flicker itself is measured per AA mode in the item — WR-15's sampling was the non-TAA half (fixed), the lamp lenses went to WR-5's sprites, the sub-pixel members the owner keeps are what remains under TAA | BOTH | few ALU/lit px | done | — |
| WR-14 | Water foam at night | BOTH | ~0 | 0.5–1 d | WR-1, WR-4 |
| WR-15 | ~~Soft shadows from dense point-light arrays~~ ✅ **judged good by the owner 2026-09-02**; sampling rebuilt the same day so it no longer needs TAA to integrate it (see the item) | VK-only | +2.3 ms measured | done | WR-8 helps |
| WR-16 | **The one ray budget** — controller, allocator, consumers, ReSTIR on top; the design is `docs/RAY-BUDGET-DESIGN.md`; ReSTIR DI is its milestone 4, WR-10 its milestone 1 | VK-only | measured targets per milestone in the design | 5 milestones, ~5 wk | absorbs WR-10, WR-17, WR-18 |
| WR-17 | Shadow rays thin with distance (+ a light cutoff), shipped as the **RT optimisation** preset: Off / Quality / Balanced / Performance — ✅ **built, measured and preset 2026-09-02; the project still at Off** | VK-only | measured on Headland: 114 → 100 / 96 / 50 ms (table in the item) | done | — |
| WR-18 | The water's rays: one mirror + refraction ray per 2x2 quad, and the refraction ray stops where the water has absorbed all but a 256th — two preset columns — ✅ **built and measured 2026-09-02** | VK-only | measured: reach −13/−3/−20 ms lossless; quads −12/−16/−13 ms with a 2x2 diff; Quality now 90/90/73 | done | rides the WR-17 preset |

**Done so far: WR-0, WR-1, WR-2, WR-3, WR-4, WR-13, WR-15, and WR-5's lamp
sprites.** The order that remains starts at WR-5's bloom audit and WR-6.

**Recommended order:** WR-0 → WR-1 → WR-2 → WR-3 → WR-4 → WR-5 → WR-6 →
WR-7 → WR-10 → WR-13+WR-8 → WR-9 → WR-14 → WR-11 → WR-12. Rationale: the
sky/ambient foundation is the cheapest big win and everything downstream
(probe, GI sky term, fog colour, water reflections) inherits it; glare is
the biggest read-as-real delta per day; the light-shape ladder
(capsule → LTC → binding) rides on corrected photometry, with specular AA
in the same milestone as LTC or lamp-lit cables and railings gain flickering
glints; volumetrics is the one real system and goes last before finishers.
Every item is independently shippable and revertible.

**WR-15 is not in that order** — found mid-plan (2026-09-01, doing WR-1), not
part of the original research pass, and its own item says it needs a design
pass before an effort estimate means anything. Slot it after WR-8 once one
of its candidate directions is chosen; nothing else in the order depends on
it.

Frame budget: the night frame is ~12.6 ms at 2560×1600 on the dev GPU.
Worst-case adds sum to ~2.5–3 ms (froxels 1–1.5 measured-class, LTC
cluster-bounded, grid likely net-negative, glare/finishers <1 combined).
**Target: hold 60 fps at 2560×1600 — stay under 16.6 ms.** Costs labelled
"inferred" must be measured (interleaved, three pairs) before they anchor
any ordering decision.

> ### ⚠ The 12.6 ms above is a RAY-TRACING-OFF number. Measured 2026-09-02.
>
> The owner saw 46 ms in the editor and asked what was in the frame. Measured
> on the committed night scene, `--benchmark`, Vulkan, **1600×900** — a third
> of the pixels the 12.6 ms figure quotes:
>
> | configuration | frame | water (Transparent) | Scene |
> |---|---:|---:|---:|
> | as committed (RT on) | **50.4 ms** (20 fps) | 32.9 | 16.5 |
> | `--raytracing=off` | **13.6 ms** (74 fps) | 6.7 | 5.5 |
>
> **13.6 ms with RT off at 1600×900 is what the plan's "12.6 ms" actually
> describes.** With ray tracing on — which is how the project is configured
> and how every capture in this plan was taken — the frame is 50 ms at a
> third of the stated resolution. The budget the WR items are ranked against
> ("~2.5-3 ms of adds on a 12.6 ms frame") is therefore measured against a
> frame nobody is looking at, and **the 60 fps target is already missed by
> 3× before a single WR item lands.** Re-baseline before using any of the
> per-item budgets to sequence work.
>
> Where the ~37 ms of ray tracing goes, isolated one effect at a time
> (same scene, same camera, one flag changed per run):
>
> | effect | cost | where |
> |---|---:|---|
> | RT shadows | **~15 ms** | ~8 scene, ~7 water |
> | RT water refraction | **~13 ms** | all water (`RayTracedWaterRefraction`) |
> | RT reflections | **~9 ms** | ~6 water, ~3 scene |
> | RT ambient occlusion | ~0.1 ms | enabled, negligible |
> | RT global illumination | 0 ms | never runs; `RayTracedGiSource: Baked` |
>
> **Shadows are the single biggest item, and the cause is structural, not a
> tuning error: 128 sodium lamps all cast shadows, the engine traces every
> casting light with no cap, and the sea fills most of the frame — so most
> screen pixels trace shadow rays toward many lamps each.** That is WR-10's
> "walks every light" problem, arriving in direct shadows before GI. It is
> also the same design that produces WR-15's shadow-streak fan: one fix
> plausibly serves both. Neither AO nor GI is worth touching for
> performance — there is nothing there to win.

---

## WR-0 · Generator parity (do this before anything touches the scene)

**Goal.** `make_bridge_scene.py` regenerates the committed scene without
losing the streak session's lighting work, so the scene stops being a trap.

**Changes.**
- `tools/scripts/make_bridge_scene.py`:
  - The 120 deck-lamp lights gain `SourceRadius: 0.6`, `Range: 600`,
    `OuterCone: 85` (read the exact current values from the committed scene
    first — `git show HEAD:SampleProject/assets/scenes/GoldenGateDemo.rage`
    — they are the source of truth, not this file).
  - The 8 lights with `SourceRadius: 0.5` likewise.
  - Add the 4 marine point lights exactly as committed (position, colour,
    `SourceRadius`, ranges).
  - Camera promotion: write the hero at `ViewRank: 0` and every other camera
    at ranks 1..n ≥ 1. Never write a negative rank (the loader clamps to
    [0,99] and −1 aliases to 0, which is how the Glitter camera silently
    stole the view).
- Optional but worth it: a `tools/scripts/check_bridge_roundtrip.py` that
  regenerates into a temp dir and asserts the lighting-relevant lines match
  the committed scene (grep for `LightComponent` blocks); wire it like other
  check scripts.

**Verify.** Regenerate with the committed scene's flags
(`--sky=night --seabed=bay --hero=headland`), run the runtime, and confirm
the log does **not** say `no bake matches` — the loader wanting
`field_73f9e1d7…` and finding it IS the acceptance (the lighting hash is the
canary). Then `git checkout --` the scene to be safe.

**Revert.** `git checkout -- tools/scripts/make_bridge_scene.py`.

---

## WR-1 · Night sky: shaped gradient, city-glow lobe, moon

**Goal.** Replace the linear horizon→zenith mix with a sky that can hold the
real night sky's structure: a thin bright horizon band, a warm glow dome over
San Francisco, dark Marin, an optional moon. Feed it to everything that
currently inherits the gradient or black: the visible sky, the environment
probe (`GradientCube`/`GradientIrradiance` in `Skybox.cpp`), the sky-occlusion
ambient term, water reflections, and (via WR-3) fog inscatter.

**Evidence.** The generator's own comment documents the workaround ("no
falloff to shape it, so… lower it until it falls under visibility"). Godot's
`ProceduralSkyMaterial` ships exactly this upgrade (`sky_curve` default 0.15
— the horizon holds in a thin band); Ghost of Tsushima's sky LUT concentrates
resolution at the horizon for the same reason. Skyglow physics (Garstang
model): the dome is brightest at the city azimuth near the horizon, falls
steeply with elevation; SF-adjacent zenith ≈ 5e-3–2e-2 cd/m², the dome near
the horizon 10–100× that; colour warm (~2000–2500 K). Real full-moon
photometry: 0.05–0.3 lux ground, disc ~0.53°, CCT ~4100 K (warm).

**Changes.**
1. `RageVEditor/assets/shaders/sky.rvshader` — the gradient evaluation (grep
   `mix(u_Params.Horizon.rgb, u_Params.Zenith.rgb`). Replace with:
   ```glsl
   float el   = saturate(dir.y);                       // elevation 0..1
   vec3  sky  = mix(horizon, zenith, pow(el, SkyCurve)); // SkyCurve ~0.35
   // additive city-glow lobe (dirXZ = normalize(dir.xz)):
   float az   = saturate(dot(dirXZ, CityDir));          // CityDir = +z (south)
   sky += CityGlowColor * pow(az, CityAzimuthK)         // K ~ 4..8
                        * pow(1.0 - el, CityElevationK); // K ~ 8..16
   // below horizon: separate ground/sea gradient, unchanged shape
   ```
   Moon disc: an analytic disc on the moon direction (angular radius
   0.0046 rad; draw 2–4× for composition if the owner wants), limb-darkened,
   emissive high enough to clip. Stars are WR-12, not here.
2. **`Skybox.cpp` mirrors the gradient on the CPU** — `GradientAt(...)`
   builds `GradientCube` (32³ faces) and `GradientIrradiance` for the probe
   path. It must implement the identical formula or the probe and the sky
   will disagree (that class of bug reads as "reflections don't match the
   sky"). Keep the two implementations textually adjacent via a shared
   constant block if possible.
3. New `Environment` fields (scene `Environment:` block →
   `ComponentRegistry.cpp`): `SkyCurve`, `CityGlowColor`, `CityGlowBearing`,
   `CityAzimuthK`, `CityElevationK`, optional `MoonDir/MoonColor/MoonDisc`.
   Defaults must reproduce the old linear mix exactly (`SkyCurve = 1`, glow
   black) so **every existing scene renders bit-identically** — that is the
   acceptance for the refactor half. Use the `FieldHint` `LegacyName`
   mechanism if any field is renamed.
4. The moon as illumination: a second directional light in the scene (the
   engine treats it as any directional; RT shadows work). Scene change, not
   engine change. Intensity 0.05–0.3 lux-equivalent (or 2–10× for read,
   GoT precedent). Colour: physically ~4100 K warm; if it reads as daylight,
   grade it cool **at the light**, and note in the scene comment that this
   is the film convention, chosen deliberately.

**Values to start from** (then grade by eye against references):
night horizon band `SkyCurve 0.3–0.5`; glow dome peak sitting 50–500× the
zenith sky luminance; glow colour ~(1.0, 0.42, 0.12) pre-exposure; dome
bearing = +z (San Francisco is south; `-z` is Marin).

**Verify.**
- All non-bridge scenes byte-identical with defaults (screenshot diff, the
  showroom and camp at minimum, both backends).
- The environment probe is no longer black: sample `GradientIrradiance`
  (or screenshot a chrome sphere) — water and steel must pick up the glow.
- Night captures: sky histogram shows the dome at roughly 1–2% grey under
  the fixed night exposure, zenith below it, Marin side darker than SF side.
- Banding will appear on the smooth dome — WR-2 is the fix, land it together.
- A bake-invalidation check: if the sky feeds the lighting hash, existing
  bakes will be refused — that is correct behaviour, re-bake with
  `--bake=force --rhi=vulkan --benchmark=8000`; if it does NOT feed the hash,
  stale bakes will silently keep the old ambient — check which, and say so
  in the PR.
- **Answer this engine question explicitly in the PR: do the irradiance
  volumes re-light against a changed sky at runtime, or only at bake?** The
  sky-occlusion term multiplies the live sky (runtime — good), but the
  field's stored bounce was solved under the old sky. If the bounce needs a
  re-bake, that is a per-lighting `--bake=force` for the night scene
  (two-flavour pair, `_rt` and `_ss`), and the plan's "~0 cost" for this
  item is bake time, not frame time. Hours vs days hinge on this — settle it
  before scheduling WR-3.

**Traps.** The sky-occlusion term multiplies the **live** sky colour — it
must multiply only the sky/ambient term, never the field's bounced light
(double-darkening). OpenGL sampler budget if any new texture is added.

**Revert.** All new fields default to the old behaviour; revert = defaults.

---

## ~~WR-2 · TPDF dither (Playdead INSIDE recipe)~~ — ✅ **done 2026-09-01**

**Goal.** Kill banding in night gradients. Sky, fog composite, and
tonemap-out each get ~1 LSB of triangularly-distributed noise at write time.

**Landed differently from the brief above, and the reason is worth keeping.**
The recipe as written — dither at all three passes' final writes — was built
and measured, not just built. Sky and fog write to a linear, pre-exposure
HDR target; "1/255" is only actually one display level *after* `Exposure`,
the ACES curve and `pow(x, 1/2.2)` gamma encoding, all of which lie between
those writes and the swapchain. Worked through the chain at a representative
night-sky value (~0.01 linear, `Exposure 2.4`) a ±1/255 linear perturbation
comes out near **10 levels of 255** at the final pixel — gamma's encoding
slope is steepest exactly in the near-black range a night sky lives in.
Rendered, not just derived: the owner saw it as faint but real grain on the
sky at normal size, and a box-blur noise measurement on the sky region
confirmed it — std 1.70 (no dither) → 3.08 (dithered at all three writes,
sky region), against a p99 that went from 5 to 8 levels.

**What shipped instead: `tonemap.rvshader` dithers once, after Exposure,
ACES and gamma — the only point in this pipeline where "1/255" is the unit
it claims to be**, because nothing downstream of it has gain left to apply.
That single dither point covers every upstream pass's writes, sky and fog
included, since they all funnel through it. Measured the same way: std 1.74
against the 1.70 no-dither floor — indistinguishable from noise already
present in the render (RT sampling variance, confirmed separately: two
renders of the same build/frame differ by at most 1 level on 0.002% of
pixels, so that floor is not run-to-run instability either). `HashU`
factored into `include/dither.glsl` so grain and dither share one
reproducible hash instead of each pass growing its own.

**Addendum, same day: `sky.rvshader` got its dither back, opt-in.** The
owner asked for a toggle rather than leaving the capability out entirely --
`SceneEnvironment::SkyDither` (default off), a spare lane in `SkyExtra`
(`CityGlowColor.a`), and the same `TpdfDither` call as before but scaled by
`kSkyDitherCompensation = 0.1` before it goes into the linear write -- the
measured ~10x overshoot, inverted, so switching it on lands close to the
same ~1 level tonemap's own dither already guarantees rather than
reintroducing the original mistake behind a flag. Stated as what it is, not
derived: the factor is measured for a typical night sky's brightness, and a
much brighter gradient would under-dither with it, which is the safe
direction to be wrong in. `fog.rvshader` was not asked for and stayed as
landed above -- no dither, no plumbing.

**The general lesson, for WR-11 and WR-12 (both add HDR-stage writes):** a
fixed additive dither is only correctly sized at the stage nearest the final
low-precision write. Anything upstream of exposure/tonemap needs either no
dither, or an amplitude scaled to survive the *specific* downstream gain at
that brightness — never the flat display-referred constant.

**Verify.** Amplified (×12) screenshot of the night sky gradient before/after:
bands gone, noise floor invisible at 1× (confirmed by the box-blur std
comparison above, not eyeballing alone — the over-strong version looked
"slightly visible" rather than obviously wrong, which a bare screenshot
comparison could easily have missed). scenetest green on both backends,
unchanged.

---

## ~~WR-3 · Fog inscatter takes the sky's colour per view direction~~ — ✅ **code done 2026-09-02, the scene's fog values are an owner call**

> **Built as a cube sample, not a shared gradient function.** The plan proposed
> moving WR-1's gradient into `include/sky_gradient.glsl` and calling it from
> `fog.rvshader`. That was not free: the gradient is seven vec4s of state and
> the fog pass's push block is already **128 bytes to the byte**, so sharing
> the function meant giving the fog a uniform buffer, and `PostProcess::
> Dispatch` has no parameter for one. Sampling `Skybox::ResolveEnvironment`'s
> cube instead costs one texture binding and four scalars -- which fitted in
> the `w` lanes of the camera rows, already being uploaded as zeros -- and it
> works for a **cubemap sky as well**, which an analytic evaluator structurally
> cannot. The cost is resolution, and see the cube-size finding below.
>
> **Two dials, not one** (owner's call): `FogSkyAffect` blends the inscatter
> colour from the constant `FogColor` toward the sky, `FogSkyOcclusion` dims
> the sky *behind* the fog so a bank can eat a skyline instead of only adding
> to it. Both default 0, which is byte-for-byte the fog this pass had.
>
> **The sample is lifted to the horizon, and the first render is why.** Read
> literally, "the sky in this view direction" hands a ray aimed at the water
> the *ground* half of the gradient: the haze over the strait came out navy
> under an amber sky. The air between the camera and a ship two kilometres out
> is lit by the low sky in that bearing, not by the sea beneath it, so the
> sample clamps `direction.y` to zero. Azimuth -- where the whole effect lives
> -- is untouched.
>
> **Two findings from doing it:**
>
> 1. **The committed night fog cannot show this feature at all.** Its layer is
>    `FogHeightFalloff: 0.11`, an e-folding height of **9 m**, and the hero
>    camera stands at **89 m**. The camera is above its own fog. Turning
>    `FogSkyAffect` to 1 on the scene as committed moves 44 k pixels by at most
>    **one level**. With a layer that reaches the towers the same dial moves
>    the frame by up to 23 levels and varies with bearing. **The fog values are
>    an authoring decision and are still the owner's** -- nothing was committed
>    to the profile but the two keys, both at 0.
> 2. **The 32-texel gradient cube under-resolves WR-1's glow lobe**, and not
>    only for the fog. `CityElevationK: 20` makes the lobe about 3° tall; a
>    32-per-face cube texel is 2.8°. Raised to 128 (a CPU bake that runs only
>    when a sky dial moves; the irradiance convolution's cost does not depend
>    on it), the fog's glow peak gains **12 levels** and the city-to-ocean
>    contrast goes from 12 to 20 levels. **The same cube is what every
>    reflective surface and every probe reads**, so they have been reflecting
>    an under-resolved glow since WR-1. `scenetest` is green on both backends
>    at 128. Left in the tree as a separate, revertible change.

**Goal.** Distance haze at night fades things toward the sky radiance *in
that view direction* — warm amber toward the city, near-black toward the
ocean — instead of one constant colour. This is the single most
characteristic feature of the real view down the strait, and it is a
shader-only change to the existing closed-form fog.

**Evidence.** Godot `fog_aerial_perspective` (fog colour from sky per view
dir), UE inscattering cubemap, Ghost of Tsushima's LUT-lit haze. Night photos:
a fog bank in front of the city reads as a dark silhouette eating the skyline
— unlit fog is an occluder, not a glow.

**Changes.** `fog.rvshader` currently reads a constant `Color` from its UBO.
Move the WR-1 gradient function into a shared include (e.g.
`include/sky_gradient.glsl`) used by both `sky.rvshader` and `fog.rvshader`;
fog inscatter colour becomes `EvaluateSky(viewDir)` (glow lobe included),
optionally × an HG(g≈0.5) moon-direction term for thin-haze brightening.
Add a `FogSkyAffect` post-profile scalar [0..1]: the fog amount must also
attenuate the sky/city behind it, or fog brightens the skyline instead of
eating it. Post-profile files are CRLF-no-trailing-newline — mind the trap.

**Verify.** Night capture toward the far tower: it recedes into warm glow
toward SF and into darkness toward the ocean (compare reference: far tower
always softer AND warm-tinted). Day scenes unchanged with `FogSkyAffect`
defaulted to current behaviour. Both backends.

---

## ~~WR-4 · Photometry & fixtures pass (scene data only, no engine code)~~ — ✅ **built 2026-09-02, awaiting the owner gate**

> **What landed.** `tools/scripts/derive_lamp_color.py` (new, committed beside
> the scene tools) does both halves: chromaticity to linear Rec.709, and the
> deck lamps' intensity solved backwards through *this engine's own* spot
> falloff from the survey's road illuminance. `make_bridge_models.py` carries
> the derived colours, `make_bridge_scene.py` the photometry and the new
> fixtures, `SampleProject/Scripts/Beacon.cs` the flash. Scene regenerated,
> re-baked, 191 lights where there were 133.
>
> **The colour was out by a gamma.** `SODIUM` was `(1.0, 0.60, 0.16)`, which is
> almost exactly `#FF8B14` *display-encoded* written into a field the renderer
> reads as linear. Derived, it is `(1.0, 0.2795, 0.0091)`. The script's own
> check: fed the 589 nm LPS point it reproduces the plan's independently
> sourced `#FF8000` to four decimals.
>
> **The lamps were about fifteen times too dim in luminous terms**, and this is
> the finding with consequences past WR-4. At `Intensity: 452` the roadway sat
> at roughly 0.03 cd/m² against a sky of 0.0225 -- a road barely brighter than
> the sky behind it, which is the wrong way round for a night exterior.
> Measured, the drivelane wants 14 lux and about 0.45 cd/m². **That makes
> `GiIntensity: 2.6` a live suspect for compensating for under-lit lamps**
> rather than for anything about the bounce.
>
> **The cone model cannot be a shoebox, and it shows in the uniformity.** Real
> cutoff street lighting runs about 3:1 max-to-average; solved into this
> engine's flat-topped cone the same average gives 81 lux under a post against
> 14 average, nearly 6:1. Not fixable in scene data.
>
> **Everything new is `CastShadows: false`, measured rather than preferred** --
> shadows are ~15 ms of the 50 ms frame with 128 casters and no cap. The whole
> pass costs **+2.3 ms** (50.4 → 52.7 ms at 1600×900) for 58 more lights.
> **The trade is real and worth stating: `CastShadows: false` is no occlusion
> test at all**, so the tower floods light the far side of the shaft they
> stand under.
>
> **120 posts, not 128.** The survey's count and its 45.72 m spacing cannot both
> hold over the 2 737 m this model builds -- 128 posts is 64 opposite-pole
> stations and needs 2 880 m. The spacing is kept (it is twelve of the 12.5 ft
> module every other dimension derives from, and the lights must land on the
> lamp geometry); the count is short by four stations that reach past the
> structure. Stated in `lamp_stations`.
>
> **A regeneration trap closed on the way through.** The generator had been
> writing `ReflectionFloor: 30` over the committed `0.5` every time the scene
> was rebuilt -- the WR-0-shaped gap the handoff names, which WR-4 had to run
> into because WR-4 regenerates. Fixed at the source, with `5d28c0c`'s
> measurement quoted beside it.

**Goal.** Set every lamp parameter from measurement instead of taste, and add
the missing fixture types. This is the "film set" half — placement and
photometry carry more realism than any shader.

**The measured spec (DOE GATEWAY report PNNL-21894 + bridge district records):**
- **Deck lamps:** exactly 128 posts, opposite-pole, 45.7 m spacing along
  traffic, mounting height 7.09 m. 250 W HPS (28,000 lm) behind amber acrylic
  lenses (fitted 1972 to preserve the original 1937 low-pressure-sodium
  colour). Working colour: **derive it, don't quote it** — the strands agreed
  on chromaticity (x≈0.51–0.52, y≈0.42, HPS-through-amber, between bare HPS
  and LPS-clipped `#FF8000`) but produced two conflicting RGB translations.
  Write a ~20-line offline script (xyY → XYZ → the engine's actual
  working-space matrix, clip negative blue, normalise), commit it beside the
  scene tools, and use its output as ONE shared constant across all 128 —
  the real bridge is deliberately uniform, and WR-9's Luminaire binding
  expects a single authored value.
  Cutoff shoebox distribution: light thrown down-road, almost no uplight
  (this is why the towers catch so little roadway light). Target: drivelane
  ~13–15 lux → ~0.3 cd/m² road luminance — genuinely dim; the deck must NOT
  look brightly lit.
- **Tower floods:** per tower, 12 uplights at sidewalk level + 12 below the
  roadway, wattage-graded 4×150 / 4×250 / 4×400 W. Implement as spot lights
  with intensity ratio 0.375 / 0.625 / 1.0 and let inverse-square do the
  fade up the shaft — do NOT paint the tower with one uniform light. Their
  colour is neutral-white HPS class, and they are the one place International
  Orange re-saturates at night.
- **Accents:** 360° flashing RED beacon atop each tower (time-varying
  emissive + light, flash cycle via a C# script — remember
  `ManagedScriptComponent`, and `Sample.dll` must be rebuilt with
  `dotnet build -c Release -o bin` from `SampleProject/Scripts`); midspan
  columns of three WHITE over one GREEN on both faces below deck; RED lights
  on the south fender; 35 W LPS post-tops (pure 589 nm, `#FF8000`) at the
  tower bases.

**GI budget note.** 128 lamps must NOT become 128 NEE emitters (cap is 16 —
`kMaxAreaEmitters` in `Renderer3D.h`). Until WR-10 lands, represent the lamp
row for GI as a handful of merged emissive strips; keep beacons/nav lights
out of the NEE set entirely.

**Verify.** Self-check from the research: swap all lamps to generic 3000 K
warm-white and diff — the whole deck's character must change, not just the
lamp sprites (that is what CRI-22 light does). Checklist items 1–3 by eye.
**[OWNER GATE]**

---

## WR-5 · Night glare — 🔨 **the lamp sprites are built (2026-09-02, third session); bloom audit and PSF weights are not**

> **Why the sprites came first.** The flicker table in the WR-13 item ends
> with the one thing no resolve could touch: the lamp lenses, 0.7 m boxes at
> emissive 26 that a headland pixel a metre wide lands on one frame and
> misses the next. `LightGlow` (`Renderer/LightGlow.cpp`,
> `light_glow.rvshader`) draws every positional light that has a
> `SourceRadius` as a soft disc a fixed few pixels across, at the end of the
> opaque pass, additive, depth-tested, reading the scene's own light buffer
> so a light needs nothing added. The disc carries **I/(d²Ω)** — the value a
> correctly integrated sub-pixel source would put in a pixel — spread through
> a profile that integrates to one, so the lamp row adds the light it always
> should have and bloom sees the same energy an ideal render would. It fades
> out where the lens itself is bigger on screen than the disc (0.5× to 1.5×
> of the disc's diameter), so close-ups keep the authored look. The flare is
> a share of that energy moved into a wider exponential halo with an optional
> star of rays, drawn as a **ring outside the lens** when the lens is big
> (the lens's image smeared outward, normalised over the annulus), so it stays
> at every distance as glare should. Six profile dials: `LightGlow`,
> `LightGlowPixels`, `LightGlowIntensity`, `LightFlare`, `LightFlareSize`,
> `LightFlareRays`; the bridge profile and the generator carry them.
>
> **Two things the first landing got wrong, both visible.** (1) Every light
> glowed at a quarter from outside its cone, and the forty-eight tower floods
> — narrow projectors behind louvres — came out as blinding white blobs on
> the tower bases and at deck level. The side glow is now scaled by the cone's
> width: a wide diffuse lens (the 85° street lamps, the 78° marine lights)
> keeps it, a narrow beam (the 58° floods) gets none. (2) The flare faded out
> with the disc, so a close lamp had no glare at all; it now persists as the
> ring above. **The owner's first verdict on the flare at physical intensity:
> "looks bad, out of place and not real"; at 0.3 and 0.1 "better"; asked for
> more toned down** — candidates at 0.05 (rays and no rays) and 0.02 were
> shot, and **the owner chose 0.02 with a plain fifth-share halo and no
> rays** ("this looks better"); that is what the profile and the generator
> carry. The physical value is 1.0 and is far brighter than the lens boxes
> ever were, because the lamps are 4 375 cd and the lens boxes were tuned by
> eye. The white channel lights under the towers (three 300 cd lanterns
> stacked per side, facing the camera inside their cone) read strongest of
> all; the owner called them overpowering at 0.05 and acceptable at 0.02.
>
> **What it does and does not fix, measured** (Headland, pinned clock,
> blink = swing ≥ 6 with reversals): the lamp heads no longer appear in the
> blink mask under any mode; no AA / MSAA stay at 0.01% of the frame. **The
> deck band under TAA stays at ~23%, and the mask says exactly where: the
> lamp posts, the railing pickets and the truss webs** — members a tenth of
> a pixel wide at a kilometre, lit brightly, present in one frame in eight.
> Coverage AA barely helps (`--msaa=4` under TAA: 22.5%) and no blend share
> can. That is content thinner than a pixel, and the fix is the one the
> generator's own railing note already describes: **members that are
> sub-pixel at a camera's distance are not drawn.** Built the same evening:
> `MaterialParams::Macro.zw` carry `FadeStart`/`FadeEnd` (metres; serialised
> under those names), and the masked variant of the lit shader discards past
> FadeEnd and on a per-pixel gradient-noise dither between the two — walked
> per frame only under a temporal filter, the soft shadow's rule. **The ray
> hit test honours the same fade** (`RayCandidateIsThere`, dithered from the
> hit point): the first landing left the rays alone and the water's mirror
> rays kept finding pickets and ropes that were no longer on screen — the
> owner's "random red dots in the water".
>
> **How far it goes is the owner's call, and the first cut went too far.**
> It faded everything under 0.3 m plus the 0.6 m truss webs — suspender
> ropes, rails, rail posts, lamp shafts and housings, webs — and from the
> headland the ropes vanished: *"the fade made the cables disappear
> completely which just makes the scene look bad."* Now only the members
> that are nothing but noise at any distance go: pickets (0.07 m), lamp arms
> (0.17 m), brackets (0.2 m) and band flanges (0.15 m), in `bridge_thin`
> (300 → 450 m, `Blend: Masked`, `AlphaCutoff: 0`), one extra entity. The
> ropes, rails, posts, lamp shafts and webs stay unfaded steel and keep
> their sub-pixel flicker under TAA; the lighting hash is untouched, so the
> bake still loads. (The round-trip guard compares against HEAD, which is
> still the pre-WR-4 scene, so it reports WR-4's 191 lights against HEAD's
> 133 until that session's scene is committed; the regeneration reproduces
> the current 191 as an exact multiset.)
>
> **And the still-pixel feedback boost in the TAA is off** (`kStillFeedback
> = 0`): a second, thousandfold tighter motion gate still let the far water
> through — near the horizon its crests move less on screen than float
> error, and its sparkle changes every frame regardless — and the owner saw
> the streaks smear into bands a second time. The moments floor stays; it
> is gated on the same stillness and leaves the water alone. The sub-pixel
> members the boost was reached for are what the fade removes.
>
> **Still open from this item:** the bloom audit and the PSF weights.


**Goal.** Above-clip luminance is the only brightness channel the display has
left at night; glare structure is how it reaches the eye. Three sub-items,
independently shippable:

**(a) Bloom hygiene audit** (Jimenez / CoD:AW). The chain
(`bloom_prefilter.rvshader` → downsample → upsample) already has a soft knee
and `KarisWeight`. Audit against: threshold **zero** (thresholdless) with a
LOW composite weight (energy-conserving lerp, bloom never adds energy);
13-tap Karis-average downsample (firefly suppression — this specifically
protects the water-streak highlights from feeding bloom flicker); tent
upsample. If the night profile needs different numbers than day, they go in
the post profile, not code.

**(b) Night PSF weights** (Spencer '95). The eye's night PSF has power-law
tails (1/θ², 1/θ³) and — only with a night-dilated pupil — a ~3° wavelength-
scaled halo. Practical version: give the bloom composite per-profile band
weights, and add one or two extra very-wide low-res pyramid levels for the
power-law veil. Night profile weights follow Spencer's scotopic split
(unscattered 0.282, more energy in the wide veil); day keeps current. All
constants — the night profile is a discrete state, no runtime logic.

**(c) Lamp flare sprites** — the unfair advantage: the bright sources are a
KNOWN list (~128 lamps + beacons). Precompute one PSF sprite offline
(radial needles + faint 3° halo with R/G/B radii scaled 614/549/466 over 568
+ power-law falloff — a small Python generator into a texture asset, minted
`.meta` like `make_terrain.py` does; never rewrite an existing meta). At
runtime splat camera-facing quads at each visible lamp's projected position,
scaled by clipped-away luminance (`max(0, L - clip)`), attenuated by the
analytic fog transmittance along the **lamp** ray (the closed-form fog
already computes this — and it must not also be fogged by the fog composite,
or fog is counted twice) and by a depth-buffer visibility test. A small
instanced pass **after the temporal resolve** and after bloom, before
tonemap — the sprites are analytic and stable, need no AA, and must not
enter TAA history (the resolve would dim them; see Ground rules).

**Cost is labelled inferred until measured:** 128 large-kernel alpha quads
with power-law tails are fill-rate-bound in the worst camera (looking down
the deck row, every lamp on screen). Before anchoring anything on
"near-zero", run a 30-minute overdraw micro-benchmark: deck camera, all
sprites on vs off, interleaved three pairs. If it bites, clamp sprite radius
by distance and skip sub-threshold contributions.

**Verify.** The acceptance image test from the research: each deck lamp =
warm core + needle structure + local veil; fog turns lamp rows into
overlapping glows with the dark sky preserved above; if the deck reads as one
homogeneous orange haze, the item FAILED. Diff against references by eye.
Watch the scene moving, not stills (bloom flicker is temporal).
**[OWNER GATE]**

**Traps.** Do not feed the sprite pass from post-Purkinje/post-local-exposure
colour (ordering: sprites and bloom read scene-referred HDR before WR-12's
operators). Double-counting: the sprite's needle continues the water streak
visually — when WR-8's LTC lands, re-tune both.

---

## WR-6 · Water sparkle: a local bound replaces the flat ReflectionFloor

**Goal.** Kill the firefly field on open water without killing the streaks.
Mechanism (owner-diagnosed, capture-confirmed): the mirror-ray bound is
`min(traced, prefiltered·8 + floor)`; the night probe is near-black, so the
bound degenerates to the flat floor, and the night profile sets
`ReflectionFloor: 30` (vs 0.05) precisely so the glitter path survives —
which re-admits every stray jittered mirror-ray hit on an emissive-26 lens
as a passing firefly. A flat floor cannot tell coherent glitter under a lamp
from a stray lens hit far from any lamp.

**Changes.** In `include/pbr_fragment.glsl`, the mirror block (grep
`mirror > 0.0` and `ReflectionFloor`): replace the flat floor with a local
bound —
```glsl
vec3 analytic = /* the sized-light specular sum this pixel already
                   computes in the lit loop (Karis representative point,
                   SourceRadius path from commit dab692d) */;
traced = min(traced, prefiltered * 8.0 + kFloorBase          // 0.05
                    + kAnalyticLicense * analytic);          // k ≈ 4–8
```
Where the analytic term predicts a streak, brightness is allowed; where it
predicts black, a stray lens hit is crushed. Reuse the already-computed
analytic sum — do not re-evaluate lights. Retire `ReflectionFloor: 30` from
the night post profile (leave the field, default it to 0.05).

**Verify.** Blinking-pixel COUNT on an open-water region across 100 frames
(the flicker protocol) — must drop by an order of magnitude; the pier
marine-light streak must survive (per-pixel diff against before, judged for
streak preservation). **[OWNER GATE]** — the sparkle/streak trade is theirs.

**Later (separate item, note only):** the honest cure is the traced path
knowing emissive **area** (a mirror ray currently reads the lamp as raw mesh
through a pinhole — one texel of emissive 26 or nothing). Converges with
WR-8/WR-9; ray cones are the full fix (also bounds the chrome-orb case).

---

## WR-7 · Capsule (source-length) specular — the cheap streak upgrade

**Goal.** A lamp row's reflection is governed by the source's extent;
`SourceRadius` (Karis sphere, shipped at `dab692d`) gives width but not
verticality/length. Add `SourceLength` + axis: the Karis representative-point
capsule (Decima/UE pattern — "most representative point": closest point on
the segment to the reflection ray, energy renormalised). ~20–40 extra ALU on
the existing punctual path; no new light type.

**Changes.** `Light.h` (`SourceRadius` neighbour fields — a spare lane exists
in `GpuLight.Direction.w`? — it's taken by radius; find the next spare lane
or widen deliberately, minding std140/430 alignment in BOTH GLSL mirrors),
`ComponentRegistry` serialization, the lit-loop specular (grep the
`SourceRadius` use in `pbr_fragment.glsl`), and the water evaluation which
already consumes the sized-light widened lobe. Scene: deck lamps get
`SourceLength ≈ 0.6–1.0` vertical (the head + bright pole-top region).

**Verify.** Streaks on water gain body/verticality at the pier and span rows
(diff + owner's eye); showroom byte-identical with every length at zero —
that null test is the acceptance for the refactor. **[OWNER GATE]**

**Note.** This is the interim; WR-8's LTC line is the correct form. Doing
WR-7 first de-risks WR-8 and may prove sufficient for the 500 m row.

---

## WR-8 · LTC area lights (rect, then line/tube)

**Goal.** Real area-light specular: correct streak width AND length from
source extent, soft highlight gradients on painted steel, floods that fall
off up the tower without cone hand-tuning. The production standard (Unity
HDRP rect/tube ARE LTC; UE rect specular is LTC).

**Changes, in order:**
1. Ship Heitz's two published 64×64 LUTs (fitted M⁻¹ in 4 channels; GGX
   magnitude + Fresnel) as engine textures through the bindless heap (mind
   the OpenGL sampler budget — bindless helps on Vulkan; verify the GL path).
   LUT fitting code is published; do NOT refit.
2. New light type `Area` (shape: rect | line | tube) in `Light.h` +
   `GpuLight` encoding + **both** GLSL scene-block mirrors + cluster list
   build (area lights ride the same per-cluster lists — HDRP's pattern).
3. Evaluation in the lit loop: rect = 4 vertex transforms + 4 edge
   integrations (~3–5× punctual ALU); line/tube = 2 endpoints + 1 closed-form
   segment integral (~2–3× punctual). Diffuse via the standard LTC diffuse
   (identity matrix path).
4. Validate rect against Heitz's WebGL reference scene first (side-by-side
   screenshots, same parameters), THEN wire line/tube for the deck rows.
5. Water: LTC tables are isotropic-GGX only. Evaluate on the sea with an
   isotropic-equivalent roughness (geometric mean of the anisotropic alphas).
   Do not attempt anisotropic LTC tables (research-grade, huge fits).
6. Shadows: none at first — clamp max smoothness on area lights (HDRP does)
   to hide the missing shadowing; the RageV-specific upgrade later is one
   ray query toward a sampled point on the segment.
7. **The streak has TWO mechanisms, and the widening survives as one of
   them.** The LTC line provides the geometric half (source extent); the
   hand-built anisotropic widening models the other half (long-exposure
   temporal integration of dancing glitter — a legitimate feature under the
   photograph target, not a deprecated hack). Leaving both at the current
   strengths double-counts, so the protocol is an A/B the owner judges:
   land LTC line lights, prepare two variants (widening retuned-down vs
   widening off), diff both against the references, owner picks. Do not
   silently delete the widening.
8. **WR-13 (specular antialiasing) lands in the same milestone.** LTC gives
   every lamp-lit railing, cable and truss edge a real highlight; without
   filtered NDF roughness those highlights are sub-pixel glints that flicker.
   The technique that fixes the water must not make the steel worse.

**Verify.** Rect-vs-reference match; 128-lamp night frame inside +0.8 ms
(measure interleaved, 3 pairs, per the perf protocol); streak row vs
reference photographs; tower flood gradient (checklist 2) once floods become
small rect/disc areas. Both backends compile; scenetest green.
**[OWNER GATE]**

---

## WR-9 · Luminaire binding: the light owns its lens

**Goal.** One authored object per lamp: the analytic (LTC) light owns and
synchronises its visible emissive lens — intensity in shared physical units
(lens luminance `L = Φ / (π · A)`), so what is seen, what is lit, what
reflects and what shadows agree by construction (HDRP "Display Emissive
Mesh" + Frostbite units pattern). Kills the drift class the showroom already
hit (NEE ignoring per-entity overrides).

**Changes.** A `LuminaireComponent` (or a flag on `LightComponent`) that
derives the lens mesh's emissive from the light's flux. 28,000 lm through a
few-hundred-cm² aperture ≈ tens of thousands of nits — that number sets the
lens emissive under the fixed night exposure (the current hand-set emissive
26 gets replaced by the derived value; expect to re-tune WR-6's constants).

**The single-counting contract** (the integration spec for WR-5/WR-6/WR-8;
without it, LTC + lenses + sprites land in sequence and the streaks visibly
brighten at each step, over-shooting the references). After this plan, a
lamp's energy reaches the water by four paths; each regime gets exactly one:

- **Roughness split.** Above a roughness threshold the LTC light provides
  ALL direct specular, and reflection rays must NOT shade the lens emissive
  (tagged lens instances return black / are skipped in the ray-query hit
  shader). Below the threshold (glassy long-exposure water), the traced
  path provides the mirror image of the lens and the LTC contribution fades
  out — this is also where the moon disc and nav-light mirror images come
  from.
- **Traced hits on the lens clamp to its authored nits** (the binding makes
  this a lookup, not a heuristic) — small bright emissives are the classic
  firefly generator; this is WR-6's cure made principled.
- **Sprites and bloom carry only above-display-clip energy**
  (energy-conserving extraction) so they never double direct specular by
  construction; the sprite is fog-attenuated along the lamp ray exactly once.
- **Reflections are traced-only in this engine — no SSR for lamps.** SSR's
  signature failure (streak vanishes when its source leaves screen) is
  maximally visible with 128 off-screen-prone sources.

Implementation: three flags on the Luminaire — `LitByAreaLight`,
`VisibleToReflectionRays`, `ParticipatesInNEE` — plus the roughness
threshold as a render setting. The lens tag must be readable from the
ray-query hit path (per-instance data; note `InstanceData` is full at 256
bytes — the tag likely rides the material, not the instance).

**Verify.** Total streak energy unchanged (per-pixel diff) when toggling the
split threshold across its range on a still frame — the paths trade, never
sum. Lamp lens brightness and its water streak move together when one number
changes. GI energy unchanged with the lens excluded from NEE while the
analytic light contributes.

---

## WR-10 · World-space light grid + stochastic light sampling at RT hits

> **2026-09-02, superseded in part by `docs/RAY-BUDGET-DESIGN.md` §4.3.5.**
> Measured with `--hit-lights=off`: the walk at hits is ~40 ms of the 114 ms
> Headland frame, a light-*read* cost (191 x 80-byte records per hit). The
> design document orders the options: A, compact cull records plus the
> on-screen cluster at the hit (exact, 1–2 days, first); B, the world grid
> with CDF sampling below (only if A leaves the walk expensive); C, a
> direct-light field for the baked lights (this scene's big win and a
> quality gain). **The light mobility enum the owner asked for on
> 2026-09-02 — Realtime / Half bake / Full bake, replacing `IsBaked` — is
> part of C, not a task of its own:** it is the setting that says which
> lights the bake stores direct light for and where it is read (Half bake:
> at traced hits only, the screen stays live; Full bake: on screen too).
> The scene's lamps stay Half bake — the car shot needs live lamps. Build
> from the design document, not from this brief.

**Goal.** GI hit shading currently walks every light
(`for (i < u_Scene.LightCount)` in `pbr_fragment.glsl`'s traced-hit path —
grep `ShadeTraced`); at 20 lights that is ~45% of the frame's flat RTGI
cost, and at 128+ lamps it explodes. The shipped answer (Unity HDRP
"Light Cluster" for RT; Metro Exodus Enhanced) is a camera-centred
world-space grid with per-cell light lists; on top of it, per-cell
importance sampling replaces the walk. This also dissolves the
16-emitter NEE cap into a per-cell budget.

**Changes.**
1. A small compute pass per frame builds the grid: axis-aligned,
   camera-centred, e.g. 64×32×64 cells over ~500×150×1000 m for the bridge
   (or log-radial; start regular), 8–16 light indices per cell, lights
   binned by range sphere. SSBO at a new scene-UBO-referenced binding —
   remember the set-0 rule: new tables go via the scene UBO's practical
   home, and every pipeline family that reads it must declare it.
2. Hit shading: hash hit position → cell; EITHER walk that cell's ≤16
   lights (strictly better than today, zero sampling theory), OR sample 1–2
   lights ∝ per-cell CDF over `power/d²` (build the CDF in the same compute
   pass) with 1/pdf weighting + 1 NEE shadow ray. Start with the plain
   cell walk; add the CDF only if 128 lamps per cell region still hurt.
   The existing temporal accumulation + range-compressed denoiser with
   per-pixel moments is the machinery that eats the sampling noise —
   same operating point as Metro/Lumen.
3. Emissive NEE emitters go into the same grid (per-cell emitter slots),
   lifting `kMaxAreaEmitters` from a global to a per-cell cap.

**Verify.** Fixture scenes (`irradiance_*`) bit-identical or within noise
(the grid is exact when a cell holds all in-range lights); showroom RTGI
cost measured interleaved before/after (expect neutral-to-negative);
sealed-room leak fixtures still 0.000 (no new light paths through walls —
the grid must respect range, not add reach). With 128 lamps in the night
scene: RTGI trace time and the hit-shading share, before/after.

**Options (owner's call, documented trade-offs):**
- **Plain grid walk** (recommended first): no bias, no noise change, pure cost fix.
- **+ per-cell CDF, 1–2 samples**: near-constant hit cost at any lamp count;
  adds sampling noise the denoiser must eat — judge on the moving scene.
- **ReGIR-style per-cell reservoirs / ReSTIR DI** (deferred): at N≈150
  homogeneous lamps the reservoir machinery buys little over a CDF and its
  reuse fights the existing GI denoiser (two temporal accumulators
  disagreeing). Revisit when (a) the scene grows thousands of independent
  emitters, or (b) the demo needs **all 128 lamps casting shadows at camera
  pixels** — point-shadow slots are 4/4, and ReSTIR DI (temporal-only first,
  M-cap ~8–20, boiling/disocclusion mitigations, ~16 B/px reservoir buffer,
  small deferred resolve for the chosen light) is the only architecture that
  delivers every-light shadows at 1 ray/px. For the static bridge, the
  cheaper alternative for shadowed lamps is an id-Tech-style cached
  shadow-map atlas (static lights, maps rendered once and reused) — evaluate
  that first.

---

## WR-11 · Froxel volumetrics (the one real system)

**Goal.** Light-in-fog: floodlight cones in mist, lamp halos with dark sky
above, fog brighter near lamps (checklist 7). Analytic height fog cannot do
any of this — it has no per-light in-scatter. Every reference photo makes
scattering the subject; NEXT.md item 7's appetite is proved.

**Design (assembled from AC4/Frostbite/TLOU2/UE shipped numbers):**
- Grid: 16×16 px tiles × 64 depth slices → ~160×100×64 at 2560×1600
  (start here; 8×8 is the quality rung at ~3× cost). Two RGBA16F volumes
  (scattering RGB + extinction; emissive RGB + phase g) ≈ 16 MB.
- Density: exponential height falloff, scale height 2–5 m above the
  waterline (marine mist hugs the water; towers rise out of it) + one octave
  of wind-advected Perlin (AC4's exact recipe). Fog "materials" composable
  later; one global + one low layer now.
- Lighting: per-froxel forward loop over the **existing cluster light
  lists** (Frostbite aligned froxel tiles to light tiles for exactly this);
  Henyey-Greenstein phase, g ≈ 0.5–0.65 for the mist layer (physical fog is
  g≈0.9 but that aliases in a coarse grid — every engine clamps; UE default
  0.2 is the low end).
- **Per-light visibility, two tiers (this is where the backend split
  lives):**
  - **Tier 1 — analytic cone mask, both backends, nearly free.** For the
    128 downward shoebox lamps, the occlusion that matters is the
    luminaire's own housing, not scene geometry: an angular mask (spot-cone
    / IES-style falloff) applied at froxel injection gives the downward
    cone and kills the glowing-sphere failure for every lamp, on Vulkan AND
    OpenGL, for a few ALU per froxel-light pair. This covers ~95% of the
    fixture count with no rays and no shadow maps.
  - **Tier 2 — one ray query per froxel toward the dominant light,
    Vulkan-only, amortised by the temporal blend.** Reserved for the ~24
    tower floodlights, whose shafts are occluded by real geometry (tower
    legs, deck). ~150–200k rays/frame at the coarse grid for the flood
    cells only — far below the naive 1M-ray estimate. OpenGL renders
    tier-1 cones without geometric shaft occlusion; if that reads wrong
    there, the shipped fallback precedent is AC4's downsampled-ESM
    (~0.4 ms class), built only if someone actually complains.
- Temporal: per-frame sub-froxel Halton jitter + ~5% exponential history
  blend, **with history clamps** (same discipline as `gi_denoise.rvshader`
  — moments/clamp, or bright shafts ghost on camera pans). Flashing
  beacons/nav lights must bypass history or be clamped hard (UE documents
  the "lighting trails" artefact).
- Integration: march slices accumulating in-scatter + Beer-Lambert into a
  second volume; apply = one tex3D fetch + lerp at the existing fog
  application point, replacing the analytic fog inside the grid range
  (analytic continues beyond). Transparents/particles sample the same
  volume (that is the TLOU2 motivation).
- Budget: ~1–1.5 ms GPU (three shipped datapoints at ~1 ms for this class
  on PS4-era hardware). Measure interleaved.

**Fallback option if the milestone can't take a system:** Sun-Ramamoorthi
2005 closed-form single-scatter airlight for the nearest N lamps (a couple
of LUT fetches + ALU per pixel per light, no grid, no memory) gives
glow-around-lamps but NOT occluded shafts. Honest halfway house; the froxels
obsolete it.

**Verify.** The three-part image test: lamp rows become overlapping glows,
dark sky above preserved, tower tops swallowed; beams from tower floods
visible from the deck camera looking up. Frame budget held. Flasher test:
watch a beacon cycle for ghosting. Fog OFF byte-identical to today.
**[OWNER GATE]**

---

## WR-12 · Display finishers

**(a) Night tone parameters.** The tonemap is ACES-approx (grep
`TonemapACES`, Narkowicz fit, in `tonemap.rvshader`). At night the whole
frame lives in the toe, and near-monochromatic sodium hue-twists / desaturates
through per-channel curves. Two options, in ascending order:
- Cheap: per-profile toe/shoulder bias around the existing curve + a
  purity-preserving highlight rolloff so clipped sodium desaturates to
  white-hot smoothly (GT SPORT's "chroma desaturates with shoulder") rather
  than banding orange→yellow.
- Full: adopt the GT7/Uchimura curve (published MIT C++, explicit toe
  strength/linear-section knobs, bakeable to a 32³ LUT with pow-indexing).
Test with a sodium-colour intensity ramp: if amber shifts green-ward or
beacons orange-ward as intensity rises, the curve fails.
An emissive-EV rolloff for lamps only (BeamNG's shipped pattern) is the
targeted alternative if the general curve stays.

**(b) Purkinje switch (default OFF).** Ghost of Tsushima's operator, 3
shader lines + two precomputed matrices (constants published: rod strengths
kL=kM=0.2, kS=0.29, opponent K=45.0, S=10.0, k3=0.6; `lmsr = M·rgb;
gain = rsqrt(1+lmsr.xyz); rgb += M2·gain·lmsr.w`). Ships as a post-profile
boolean for the retina-vs-photograph demonstration; per §0 it stays off for
the judged target. Never apply to the bloom/sprite input.

**(c) Local exposure (only if deck readability under lamps demands it).**
Tsushima bilateral grid: 64×32×64 log-luminance, `Io = c(B−M) + d(Ii−B) + M`,
**40% bilateral / 60% wide-Gaussian blend** (the halo fix — mandatory around
128 point lamps), highlight-side compression only (~0.75), shadow side
pinned at 1.0 (never lift night). ~0.3 ms compute. Ordering decision made
explicit: bloom/sprites read PRE-local-exposure colour (UE got this
wrong-by-default; their cvar exists because of it).

**(d) Stars.** Screen-space overlay in `sky.rvshader` from a small catalog
LUT (bright ~200 stars only), multiplied by `(1 − skyglow)` and a
moon-proximity fade. Physically, almost none survive this scene's exposure —
sparse-and-dim is correct; no Milky Way (Bortle 8–9). Half a day, after WR-1.

---

## WR-13 · The bridge's flicker — 🔨 **measured per AA mode 2026-09-02 (third session); the AA-independent half is fixed, the TAA half is a candidate**

> **Read this before spending another hour on the bridge's flicker.** The
> specular antialiasing below is built, correct and moves nine pixels; the
> flicker is two separate mechanisms, and the table says which is which. The
> owner's constraint for the fix: *it has to work no matter which AA is
> picked.*
>
> ### The harness, and the trap the first numbers fell into
>
> `--frame-time=0` is the **wall clock**, not a frozen clock (`Application.
> cpp`: a step of zero falls back to the measured frame time), so every
> "time frozen" arm of the earlier session was animated — the waves rolled,
> the tower beacons flashed, and the water's 15–65% "blinking" was wave
> motion. Pin with `--frame-time=0.000001`. With that, a hard-shadowed frame
> blinks at **0.01%**, which is the metric's floor. Headland camera,
> 1600×900, frames 240–255, `--render-defaults=off` plus one flag per arm,
> blinking = swing ≥ 6 levels with ≥ 2 reversals; per region, because the
> whole-frame number mixes the water in.
>
> | arm | tower | deck | cables | water near |
> |---|---|---|---|---|
> | no AA, as shipped | 0.0% | 14.5% | 0.0% | 65% |
> | no AA, soft lamp shadows forced hard | 0.0% | 0.05% | 0.0% | 0.03% |
> | MSAA 4×, as shipped | 0.0% | 23.8% | 0.0% | 65% |
> | MSAA 4×, shadows forced hard | 0.0% | 0.00% | 0.0% | 0.04% |
> | TAA, as shipped | 34.5% | 32.6% | 1.9% | 26% |
> | TAA, jitter scale 0 | 0.0% | 7.6% | 0.0% | 22% |
> | TAA, all ray tracing off | 40.0% | 27.7% | 1.7% | 40% |
> | no AA, RTAO off / reflections off | 0.0% | 14.6% | 0.0% | 65% |
>
> ### Mechanism 1 — under no AA and MSAA, all of it was WR-15's sampling
>
> The soft lamp shadow aimed its one ray at a point hashed from pixel, light
> **and frame**, so every lit pixel was re-rolled every frame and only TAA
> ever averaged it. That is a shading term that works under one AA mode —
> the defect is in the term. **Fixed, and AA-independent:**
> `TraceShadowSoftFrom` now shifts an R2 low-discrepancy point set (indexed
> by the light, so the lamps in range of a pixel stratify the disc between
> them) by a per-pixel interleaved-gradient-noise value, and walks the shift
> by the golden ratio per frame **only while `u_Scene.Jitter` is non-zero**
> — the one signal that a temporal accumulator exists. No AA / MSAA: **5.8%
> of the frame → 0.01%**, identical to hard shadows, penumbra kept; the
> Glitter-camera stills before and after are visually the same. Under TAA
> the water improves (26 → 19%) and the resolve integrates the shift.
>
> ### Mechanism 2 — under TAA, jitter plus a resolve that rejects the history
>
> With the jitter off, the tower and cables blink at exactly zero. With it,
> the per-pixel series on a tower rib reads `19 19 19 47 19 19 19 45` on the
> Halton cycle: a rib, rope or picket thinner than a pixel is rasterised only
> in the frames the jitter lands a sample on it; in the other frames the 3×3
> box is all background, the clip pulls the history to the background, and
> the tenth of the rib that entered the blend is thrown away. The pixel
> never accumulates it. Ray tracing plays no part (all rays off: 40%).
>
> **Built and measured, left in the tree as candidates because they are
> TAA's own dial** (`taa_resolve.rvshader`, `TemporalResolve` +
> `FrameGraphBuilder.cpp`):
>
> | TAA variant | tower | deck | cables | water near | full frame |
> |---|---|---|---|---|---|
> | as shipped | 34.5% | 32.6% | 1.9% | 26% | 3.59% |
> | + per-pixel temporal moments (the GI denoiser's floor on the clamp) | 27.7% | 32.5% | 1.9% | 18% | 2.81% |
> | + still-pixel feedback, capped at 0.94 by a 16-frame warm-up (the first landing) | 20.0% | 29.5% | 1.4% | 9% | 1.89% |
> | + still-pixel feedback 0.97, warm-up cap 64 | 8.6% | 25.1% | 0.9% | 1.6% | 0.98% |
> | **+ still-pixel feedback 0.98, warm-up cap 64 (what is in the tree)** | **6.0%** | **23.2%** | **0.7%** | **0.5%** | **0.78%** |
>
> The moments do what they were built for — those pixels now keep their
> history and settle on the coverage-weighted mean — and what remains is the
> blend's own share of the current frame: one sample in four at 20–70× the
> mean moves the pixel by a tenth of the difference at 0.9 and a fiftieth at
> 0.98. **The warm-up cap was the trap**: `alpha = max(1/frames, 1 - f)`
> with frames capped at 16 floors alpha at 0.0625, so a still feedback above
> 0.9375 did nothing — three arms came back identical to the pixel before it
> was found. Feedback alone (no moments) was measured too: 0.97 → tower 17%,
> 0.8 → 48%. `--msaa=4` is honoured under TAA as a measurement flag; with
> the moments and the capped feedback it took the tower to 11%. Under no AA
> and MSAA every variant measures 0.01% before and after, so nothing here
> reaches the other modes.
>
> ### What is left, and it is the same thing in every variant
>
> **The deck band stays at ~23% under TAA because of the lamp lenses**: a
> sub-pixel emitter at 26 HDR that the jitter (or MSAA's sample pattern, or
> any motion) lands on one frame in two or four. No blend share tames a
> quarter-pixel source that bright, and no AA mode is immune in motion. The
> AA-independent answer is **WR-5's lamp flare sprites** — a minimum
> on-screen size, energy-conserving — which is where this hands over.
>
> ### Corrections to the earlier note, so nobody re-derives them
>
> - "Scene animation is not the cause" was measured on animated frames. True
>   for the deck band, untestable for the water as taken.
> - "The residual ~8% is stochastic ray sampling … RTAO and the traced
>   reflections" — it was WR-15's shadow sample (100% of it) and wave motion.
>   RTAO and reflections moved the count by zero when parked.
> - "TAA roughly doubles it" — TAA is the whole of the tower's and cables'
>   flicker and none of the deck's under other modes; they are different
>   mechanisms, not one scaled.
> - The specular antialiasing stays: a handful of ALU and groundwork for
>   WR-7/WR-8, and it was never the lever.

## WR-13 · Specular antialiasing (lands with WR-8, or before it)

**Goal.** Filtered NDF roughness so sub-pixel highlights on thin glossy
geometry (cables, suspender ropes, railings, truss edges — most of this
bridge) stop flickering. MSAA 4x (the owner's deliberate setting) solves
geometric silhouettes but cannot fix shading aliasing; night makes it worse
because lamp-lit glints are high-energy and isolated.

**Changes.** Tokuyoshi & Kaplanyan, "Improved Geometric Specular
Antialiasing" (I3D 2019) — the production version of Kaplanyan 2016: widen
the GGX/Beckmann roughness per pixel from halfway-vector derivatives
(`dFdx/dFdy`); the paper gives the drop-in formula. A few ALU in
`pbr_fragment.glsl`'s specular path, applied to analytic lights (punctual,
capsule, LTC alike). Note the RT path cannot use screen derivatives — it
already carries its own bounds (the probe-bounded mirror, WR-6); do not try
to share the mechanism.

**Verify.** The moving-scene test (stills hide it): pan the deck camera along
the railing under the lamps — glint flicker before, calm after; per-pixel
diff on stills should be small and confined to highlight edges. Also expect
it to calm rough-surface RT-reflection fireflies feeding bloom (shared
mechanism). Both backends; showroom must stay visually unchanged (its
roughness maps are authored — diff and judge).

---

## WR-14 · Water foam at night

**Goal.** The foam buffer exists (built for the day look) and no research
strand or current tuning says what it does after dark. Physics: foam is a
near-Lambertian high-albedo (~0.5–0.9) diffuse reflector — the OPPOSITE of
the sea surface. Inside lamp pools it reads as bright warm flecks (among the
brightest non-source pixels on the water); away from lights it is lit only by
skyglow and goes nearly black; where it crosses a lamp streak it BREAKS the
specular column (diffuse replaces Beckmann). A streak running unbroken
through foam, or foam glowing in unlit water, are both instant tells.

**Changes.** Shader-level, in the water shading path (`water.rvshader` /
`water_foam.rvshader`): foam albedo shaded by the same clustered lights +
sky term as any diffuse surface (verify it is — it may currently carry a
day-tuned constant term); the foam mask kills/attenuates the specular and
streak terms where it covers; coverage parameter per profile — whitecap
fraction follows Monahan's wind power law (W = 3.84e-6 · U₁₀^3.41 ≈ 0.1–1%
at the strait's 5–10 m/s evening winds: sparse streaks and patches, not
carpets). Under the long-exposure target, prefer low-frequency smeared foam
textures over crisp instantaneous whitecaps.

**Verify.** Two assertions for the diff review: no foam pixel brighter than
its local lighting justifies (dark water ⇒ dark foam), and no specular
streak crosses a foam patch intact. **[OWNER GATE]**

---

## ~~WR-15 · Soft shadows from dense point-light arrays~~ — ✅ **built 2026-09-02, judged good by the owner the same evening**

> **The defect was worse than "a streak fan at the tower base".** With 120
> lamps each a point to the shadow ray, the deck railing cast 120 crisp
> shadows onto the water and the glitter path came out as rectangles with the
> streak missing inside them. The owner reported it twice, from two cameras,
> and it reads as a reflection artefact rather than a shadow one, which is why
> it survived.
>
> **One ray per light, aimed at a point on the source disc** rather than N
> rays averaged: with 120 casting lamps, multiplying the frame's most
> expensive term by N is not a trade anybody would take, and a dense array
> integrates its own samples — a penumbra *is* the partial visibility of an
> extended source. `TraceShadowSoftFrom` in `pbr_fragment.glsl`, seeded from
> pixel ^ light ^ the frame counter the traced bounce already hashes.
>
> **The frame term is load-bearing.** Seeded from pixel and light alone — to
> keep frame N reproducible — it came back as heavy salt-and-pepper. One
> sample per light needs something to integrate it and TAA is what integrates
> it; the counter used is a frame *index*, not a clock, so frame N is still
> the same picture every run.
>
> **+2.3 ms** (52.7 → 55.0 ms at 1600×900), all of it ALU: a hash, a frame
> build, sin/cos and a sqrt per shadow ray. The trig is the first thing to
> replace if that needs winning back. Both backends green.
>
> **The sampling was rebuilt the same day (third session), and the reason is
> the flicker table in the WR-13 item.** Hashing pixel, light and frame as
> white noise made the penumbra a property of the AA mode: under TAA the
> resolve averaged it; under MSAA or no AA every lit pixel of the roadway and
> the water was re-rolled every frame — **14.5% of the deck's pixels blinking
> with no AA, 23.8% under MSAA 4×, 0.05% with the ray forced hard**. Now: a
> per-pixel interleaved-gradient-noise shift of an R2 point set indexed by
> the light (the lamps in range of a pixel stratify the disc between them),
> advanced per frame only while `u_Scene.Jitter` says a temporal filter is
> running. No AA / MSAA: 0.01%, the metric's floor, with the penumbra kept
> and the stills visually unchanged. The frame term is gated on the jitter
> rather than on a setting because the jitter is the one thing a
> `TemporalHistory` cannot exist without (FrameGraphBuilder: no history, no
> jitter).

## WR-15 · Soft shadows from dense point-light arrays

**Goal.** Kill the fan of hard-edged shadow streaks radiating across the
water from the tower's base — found by the owner 2026-09-01, looking at
WR-1's first baked render from the Headland camera. Not on the original
research pass; found in the course of doing WR-1, not caused by it (the
mechanism is independent of the sky).

**Mechanism (confirmed by reading the shading path, not guessed).**
`pbr_fragment.glsl`'s lit loop traces one hard shadow ray per light, straight
to that light's exact position (grep `TraceShadow` under `RV_RAY_SHADOWS`,
~line 3446) — no area sampling, no penumbra. Near a tower, dozens of the 128
sodium lamps are simultaneously in range of a given patch of water (`Range`
went to 600 m in the streak session, `dab692d`, specifically so lamp
reflections would reach the water) and every one of them independently
decides "occluded" or "not" against the tower's mass. Moving across the
water crosses a different lamp's occlusion boundary at each point, so the
sum steps through a staircase of hard edges instead of following one smooth
penumbra — the "streak fan." This is generic to *any* large occluder near
enough lamps at once, not bridge-specific.

**Why WR-8 doesn't cover it.** WR-8 upgrades the lamps' *specular* response
(LTC, for the reflection streak) and explicitly defers shadow softness for
area lights: "Shadows: none at first — clamp max smoothness on area lights
... to hide the missing shadowing." That hides the symptom on the lit
surface WR-8 touches; it says nothing about the *shadow* this item is about,
which is a property of `TraceShadow`'s occlusion test, not the specular lobe.

**Not designed yet.** Candidate directions, untested, in ascending cost:
- **Fewer simultaneous casters near a large occluder** — a tighter
  shadow-casting range than the lit range (a light can illuminate further
  than it casts a *sharp* shadow), cheapest, changes the physical claim
  least.
- **Jittered/multi-sample shadow rays per light**, averaged — turns the
  staircase into noise instead of bands; needs the temporal filter to eat
  the noise, same discipline as RTAO/RTGI.
- **True area-light penumbra** (a ray toward a sampled point on the source,
  per WR-8's own "later" note) — the honest fix, and the most expensive.

**Verify.** Not written yet — needs a fixture with a known occluder and a
dense caster row (the tower/lamp geometry already qualifies) and the
blinking-pixel/banding-count protocol used elsewhere in this plan; a static
frame can show band count directly (edges in a horizontal scanline under
the tower). **[OWNER GATE]** on whichever direction is chosen — softening
shadows changes the picture's whole night character, not just one artifact.

---

## WR-16 · The one ray budget (ReSTIR DI inside it) — the design is `docs/RAY-BUDGET-DESIGN.md`

> **2026-09-02, owner: "make that WR-16, because now ReSTIR is not alone
> anymore."** WR-16 is the whole combined system, designed in
> `docs/RAY-BUDGET-DESIGN.md`; the ReSTIR DI brief below is milestone M4 of it
> — the second implementation behind the shadow-ray consumer's interface, with WR-17's thinning as the first, the pre-check (a fixed
> 1/2/4/8-ray budget under the three AA modes) before the reservoirs, and
> the reconstruction contract that keeps its history from fighting TAA and
> the GI denoiser. The design below stays as the reference; build from the
> design document.

**Owner-set 2026-09-02.** This plan originally deferred ReSTIR (see the
rejected list below, which is now amended rather than deleted — the reasoning
there was sound on the evidence it had). Two things changed: the owner wants
it, and a measurement arrived that the original decision did not have.

**Goal.** Shade the 128 lamps' direct lighting from roughly one shadow ray
per pixel instead of one per light per pixel, by keeping and reusing light
samples over time and across neighbouring pixels.

**The evidence that reopened it (measured 2026-09-02, see the frame-budget
box in §2).** Ray-traced *shadows* are the single largest item in the night
frame — **~15 ms of ~50 ms**, more than refraction (13) or reflections (9).
The cause is structural: every lamp casts, the engine traces every casting
light with no cap, and the sea fills most of the frame, so a typical pixel
traces shadow rays toward many lamps. That is precisely the workload ReSTIR
DI exists for, and it is not a workload the grid+CDF answer in WR-10 fully
solves — a grid tells a pixel *which* lights are near, but a pixel next to
128 near lamps still has 128 candidates to shadow.

**Scope: DI only. GI stays rejected**, and for the unchanged original
reason — a second temporal reuse scheme fights the GI denoiser's own
history, and this engine has one already (`gi_denoise.rvshader`). Do not
bundle them.

**Design (Bitterli et al. 2020; the SIGGRAPH 2023 course is the current
reference).**
1. **Candidate generation** per pixel: sample a handful of lights (8–32)
   proportional to something cheap — `power/d²`, or WR-10's per-cell CDF if
   that lands first. This is where WR-10 and this item cooperate rather than
   compete.
2. **A reservoir** per pixel: one surviving sample plus its weight and a
   sample count `M`, ~16 bytes. Reservoir sampling picks the survivor in one
   pass without storing the candidates.
3. **Temporal reuse**: combine with the reservoir this pixel held last frame,
   reprojected. **Cap `M` (~8–20)** or old samples dominate and the image
   stops responding to change.
4. **Spatial reuse**: combine with a few neighbours' reservoirs, one or two
   passes. Neighbours must be rejected on normal and depth or light leaks
   across silhouettes.
5. **One shadow ray** to the surviving light, then the existing denoiser.

**Traps, most of which are the known failure modes rather than guesses:**
- **Boiling** — reuse feeding itself into flicker on a static image. The
  M-cap and per-pixel randomisation are the standard defences.
- **Disocclusion** — reprojected reservoirs are invalid where geometry was
  hidden last frame; they must be discarded, not blended.
- **Correlation with TAA.** Two temporal systems on one image is exactly the
  trap that got ReSTIR GI rejected. Measure the pair, not either alone.
- **The flashing-emitter rule already in Ground rules applies here too**: the
  tower beacons and nav flashers must bypass or hard-clamp the reservoir
  history like every other history buffer.
- **Bias.** The weighting when combining reservoirs is where correctness
  lives; a plausible-looking image with wrong weights is the standard way
  this goes wrong silently. The sealed-room leak fixtures must stay at 0.000.

**Verify.** A fixture with the lamp row and a known occluder: noise measured
as blinking-pixel count over 100 frames (the flicker protocol), shadow
correctness against a brute-force all-lights reference on a still frame, and
the frame cost measured interleaved three pairs against today's ~15 ms. It
has to *beat* the number above to be worth its complexity — say so plainly if
it does not. **[OWNER GATE]**

---

## WR-17 · Shadow rays thin with distance — ✅ **built and measured 2026-09-02 (fourth session); shipped as the RT optimisation preset (Off / Quality / Balanced / Performance), the project still at Off**

**Owner-set 2026-09-02, in place of WR-8 as the first frame-time item.** The
owner's question was why area lights at all: "how about we just use less
rays for light sources beyond a certain distance … the farther they are the
lesser they contribute." WR-8's LTC line lights would have bought frame time
only by collapsing the far lamp row into a few lines, which changes the
picture (per-lamp streak columns become a band); this keeps every light as
it is and thins only the rays. **One render setting drives every light** —
the owner refused per-light dials — and the falloff shape is chosen by
measurement, not by argument.

**Why it is the first item.** The 1440p baseline (HANDOFF, fourth session):
a water pixel walks up to 178 of the 191 lights and traces a soft shadow
ray (WR-15) to each; scaling the third session's isolation, shadow rays
are about a third of the sea cameras' 90–130 ms frame. From half a
kilometre, a picket's shadow projects to nothing.

**Mechanism** (`pbr_fragment.glsl`, `ShadowRaySkip` / `ShadowRayKept`;
`RenderSettings::ShadowRayFade` + start / end / share; `--shadow-rays=`
for a run). Per light in the lit loop, on the traced path only: a skip
fraction from the light's distance to the shaded point, and a **fixed
per-pixel dither** (interleaved gradient noise, its own offset, R2-shifted
per light, walked per frame only under a temporal filter — WR-15's rule)
decides whether this pixel traces the ray or counts the light lit. Under
no AA / MSAA the pattern holds still; under TAA it integrates to the
fraction. Shapes: Hard (all rays to the end, none past), Linear,
SmoothStep, Log (`log2(1+t)`, most of the thinning early), and **Share** —
the physical form of the owner's rule: a light whose unshadowed share of
the pixel's direct diffuse irradiance is under a floor earns no ray, past a
start distance that protects the deck's own lamps. Share costs a cheap
pre-loop over the cell's lights (falloff, cone, cosine; no BRDF, no ray),
uniform-branched off under every other shape. Directional lights are
untouched. Off is bit-identical to before.

**The second lever, owner-set the same evening: a complete cutoff.** "A
complete cut off for light sources above a certain distance ... their
contribution to the scene is minimal or negligible."
`RenderSettings::LightCutoffDistance` (`--light-cutoff=<m>`) clamps every
positional light's range on the frame's copy before the cluster grid bins
it, so a lamp past the cutoff leaves every cell and every pixel's loop --
no shading, no ray -- and the existing falloff window takes it to zero
smoothly there. The scene's ranges and the lighting hash are untouched, and
the clamp is skipped while baking. The caveat is what the 600 m range was
set for: the Headland streaks' mirror points sit 400-600 m from the lamps,
so a cutoff under that removes them, and the diff will show where.

**What a skipped ray counts as — found by the first matrix rows, the same
evening.** The first landing counted a skipped light *lit*. Hard 150/300
on Headland: 114 → 83 ms, and the diff image was one clean band — the
water under and beside the deck brighter by up to 73 levels (5.0% of the
frame over 2 levels, 2.2% over 6; Pier 10.3% / 2.9%). The far lamps' rays
that were skipped were exactly the rays the deck itself was blocking:
each far lamp is negligible alone, a hundred of them behind one slab are
not, and "lit" put their sum back. The owner's rule holds per light and
fails per group. So a skipped light now **borrows** the mean visibility
of the far rays this pixel did trace (`farTraced` / `farVisible` in the
lit loop; only thinned lights feed the pool, a near lamp's picket is not
the deck) — the lamps sit along one deck, their visibility is correlated,
and the traced fraction estimates the group. Before any far ray was
traced it is lit. The flag's trailing `lit` token keeps the old arm for
measurement (bit 16 of the shape lane); the matrix's `+borrow` rows are
the ones the shape is judged on.

**The matrix** (`tools/scripts/shadow_ray_matrix.py`): each shape at a
gentle and an aggressive distance pair (150/300, 300/600; share floors 2%
and 5%), the cutoff alone at 450 and 300 m, and linear 300/600 with the
450 m cutoff, on Headland, Pier and Glitter; frame time from `--benchmark`,
and a still at frame 60 with the clock pinned, diffed per pixel against
the untouched frame (pixels moved by more than 2 and more than 6 levels;
the amplified difference image written beside the stills). Bar: the diff
near zero, per the standing rule. Then the winner gets the full
eight-camera table and the flicker protocol under all three AA modes.

**Results, first arm (skipped = lit) and the cutoffs, 2026-09-02.** 150
frames per cell at 2560x1440; ms, then the percentage of pixels moved by
more than 2 and more than 6 levels of 255 against the untouched still.

| variant | Headland | Pier | Glitter |
|---|---|---|---|
| off | 114.0 / ref | 100.3 / ref | 107.3 / ref |
| hard 150/300 | 83.4 / 5.00 / 2.17 | 81.0 / 10.31 / 2.88 | 74.2 / 2.71 / 0.99 |
| hard 300/600 | 115.4 / 0.00 / 0.00 | 100.7 / 0.00 / 0.00 | 108.1 / 0.00 / 0.00 |
| linear 150/300 | 81.6 / 6.77 / 3.74 | 76.7 / 23.51 / 9.65 | 71.7 / 3.02 / 1.29 |
| linear 300/600 | 100.4 / 1.84 / 0.21 | 92.7 / 2.45 / 0.80 | 94.1 / 2.14 / 0.54 |
| smooth 150/300 | 81.7 / 6.67 / 3.62 | 77.1 / 23.04 / 9.29 | 71.9 / 2.99 / 1.27 |
| smooth 300/600 | 99.4 / 1.25 / 0.09 | 93.0 / 1.97 / 0.52 | 92.7 / 2.03 / 0.45 |
| log 150/300 | 81.9 / 6.83 / 3.80 | 76.7 / 25.31 / 10.40 | 71.9 / 3.04 / 1.31 |
| log 300/600 | 98.7 / 2.37 / 0.42 | 92.3 / 2.79 / 1.10 | 92.3 / 2.26 / 0.63 |
| share 2% | 98.4 / 5.64 / 2.72 | 87.8 / 20.10 / 8.57 | 93.5 / 2.81 / 1.05 |
| share 5% | 97.4 / 7.01 / 3.75 | 84.5 / 32.57 / 12.42 | 88.5 / 3.10 / 1.27 |
| cutoff 450 m | 77.4 / 4.63 / 0.89 | 78.7 / 3.61 / 1.55 | 62.8 / 9.41 / 3.59 |
| cutoff 300 m | 50.4 / 6.24 / 3.23 | 56.3 / 15.08 / 4.66 | 38.9 / 10.89 / 4.09 |
| linear 300/600 + cutoff 450 | 75.3 / 4.35 / 0.69 | 76.8 / 3.15 / 1.37 | 59.7 / 9.43 / 3.58 |

What the arm says: (1) hard 300/600 is a clean null — the lamps' range is
600 m, so a cutoff there changes nothing, and the pipeline reports exactly
zero. (2) Under "skipped = lit" the *shape* barely matters: every 150/300
shape lands at 81–83 ms on Headland with 6.7–6.8% moved, because nearly
every ray past 300 m goes either way; what matters is the end distance and
what a skipped ray counts as. The diff is one band — the water under and
beside the deck brighter (Pier, which sits under the deck, moves 23–33%).
(3) Share buys the least for its change: its pre-loop costs, and a light's
share is small exactly where a hundred small shares add up. (4) The cutoff
is a different kind of change: its diff is *darker*, the far glitter band
along the deck's reflection dimming, and it leaves the deck's shadow alone
— 450 m takes Headland to 77 ms with 0.9% over 6 levels, but removes a
third of the Glitter camera's mid band, which was placed 500 m out to see
that streak.

**Second arm: a skipped light borrows the mean visibility of the far rays
the pixel did trace.** Same protocol.

| variant | Headland | Pier | Glitter |
|---|---|---|---|
| linear 150/300 + borrow | 80.4 / 5.42 / 1.68 | 76.3 / 9.99 / 5.34 | 71.3 / 2.95 / 1.22 |
| smooth 150/300 + borrow | 81.5 / 5.42 / 1.80 | 77.0 / 10.99 / 5.82 | 72.3 / 2.94 / 1.22 |
| log 150/300 + borrow | 82.4 / 5.49 / 1.79 | 77.2 / 11.16 / 5.57 | 72.0 / 2.97 / 1.24 |
| **linear 300/600 + borrow** | **99.6 / 0.18 / 0.00** | **92.5 / 0.53 / 0.01** | 94.0 / 1.86 / 0.41 |
| log 300/600 + borrow | 99.4 / 0.34 / 0.00 | 92.8 / 0.76 / 0.02 | 93.0 / 1.98 / 0.50 |
| share 2% + borrow | 98.8 / 3.15 / 0.54 | 88.5 / 12.15 / 5.92 | 94.1 / 2.56 / 0.83 |
| share 5% + borrow | 98.1 / 4.45 / 1.66 | 85.0 / 17.56 / 8.66 | 89.7 / 2.87 / 1.07 |
| log 150/300 + borrow + cutoff 450 | 63.4 / 5.21 / 2.03 | 65.0 / 8.94 / 4.27 | 45.3 / 9.68 / 3.79 |

What it says: (1) **Borrowing works where the far group is still sampled
and fails where it is not.** Linear 300/600 thins the 300–600 m lamps and
still traces half of them at 450 m: Headland 0.18% moved and nothing over
six levels, Pier 0.53% and 0.01% — the bar. Every 150/300 shape reaches a
skip of 1 at its end, so past 300 m no far ray is traced, the pool holds
only the 150–300 m lamps, and those are lit where the far ones sit behind
the deck: the large changes halved (Pier 9.7 → 5.3%) and no more. Hence
the **floor** (`ShadowRayFloor`, the w lane for the distance shapes): a
fraction of far rays always traced, so the estimate is consistent however
far the light; it is the aggressive dial. (2) **Glitter's residual is not
the deck.** That camera reads the far streaks, and each far lamp's streak
is modulated by the railing's soft shadow (WR-15) — per-lamp information
no borrowed mean recovers; 1.9% over 2 levels and 0.4% over 6 is the price
of not tracing it. (3) Share is out: its pre-loop costs as much as the
600 m shapes and it changes more, because a light's share is small exactly
where a hundred small shares add up. (4) The shape between start and end
never mattered more than a millisecond or a tenth of a percent: linear,
smoothstep and log are within noise of each other at both distance pairs.
Linear stays as the default for being the one with nothing to explain.

**Third arm: the floor, still borrowing the global mean.** A fraction of a
far light's rays always traced past the end.

| variant | Headland | Pier | Glitter |
|---|---|---|---|
| linear 150/300, floor 1/8 | 96.0 / 3.95 / 1.29 | 84.6 / 5.90 / 2.35 | 85.4 / 2.65 / 0.98 |
| linear 150/300, floor 1/16 | 93.7 / 3.48 / 1.39 | 83.7 / 7.81 / 3.46 | 83.0 / 2.76 / 1.06 |
| linear 150/300, floor 1/32 | 90.3 / 3.35 / 1.47 | 81.7 / 8.48 / 4.09 | 79.2 / 2.81 / 1.09 |
| log 150/300, floor 1/8 | 97.0 / 4.05 / 1.39 | 84.6 / 6.64 / 2.44 | 84.8 / 2.67 / 0.99 |
| hard 150/300, floor 1/8 | 97.7 / 2.93 / 0.57 | 88.3 / 4.73 / 1.34 | 86.9 / 2.40 / 0.81 |
| linear 150/300, floor 1/8 + cutoff 450 | 70.1 / 5.66 / 2.16 | 69.6 / 6.90 / 2.95 | 51.6 / 9.63 / 3.68 |

Two things it says. **The far rays are the expensive ones**: keeping one
in eight of them costs 16 ms of the 34 the thinning saved on Headland
(80 → 96), because a ray toward a lamp half a kilometre away over the
water traverses far more of the bridge than a ray to the lamp overhead.
And **the global mean fails the other way once the far group is
sampled**: Headland's diff gained a *darker* band on the foreground water
(2.5% of the frame) — a pixel that sees the lamps on its side of the tower
and not the ones behind it averages both groups, and the visible lamps
pay for the blocked ones. Hence the local borrow (fourth arm): a skipped
light takes the visibility of the last thinned light the pixel traced —
consecutive light indices are consecutive stations along the deck, and
neighbours share their occluder.

**Fourth arm: the local borrow** (a skipped light takes the visibility of
the last thinned light this pixel traced; the rule now in the shader).

| variant | Headland | Pier | Glitter |
|---|---|---|---|
| linear 150/300, no floor, local | 81.8 / 5.60 / 2.13 | 77.2 / 9.60 / 5.04 | 72.1 / 2.95 / 1.23 |
| linear 150/300, floor 1/8, local | 95.8 / 3.53 / 1.57 | 84.7 / 4.35 / 1.71 | 85.0 / 2.65 / 0.97 |
| linear 150/300, floor 1/16, local | 92.9 / 3.47 / 1.73 | 83.0 / 6.56 / 2.81 | 82.3 / 2.77 / 1.06 |
| **linear 300/600, local** | **100.3 / 0.32 / 0.01** | **93.6 / 0.59 / 0.03** | **94.3 / 1.93 / 0.41** |

It did not change the answer: at one traced ray in eight the residual is
the same size as under the global mean and only its sign moved (the mean
darkened lamps that were visible, the neighbour brightens ones that were
blocked; Pier improved 5.9 → 4.4%, Headland 3.9 → 3.5%). **The
per-lamp visibility field at a water pixel under the bridge — the deck
edge, the pickets, the towers, each lamp's ray on its own path — cannot be
reconstructed from one sample in eight by any borrowing rule. From one in
two it can.** That is the finding of the whole matrix, and it is a
property of the scene, not of the shape: the shape between start and end
never mattered, the fraction of far rays traced is everything.

**The menu, for the owner to pick from** (Headland / Pier / Glitter, ms
and the two diff percentages; the untouched frame is 114 / 100 / 107):

| setting | Headland | Pier | Glitter | what changes |
|---|---|---|---|---|
| **linear 300/600** (floor moot) | 100.3 / 0.32 / 0.01 | 93.6 / 0.59 / 0.03 | 94.3 / 1.93 / 0.41 | nothing the eye finds: −13% for a diff at the floor |
| linear 150/300, floor 1/8 | 95.8 / 3.53 / 1.57 | 84.7 / 4.35 / 1.71 | 85.0 / 2.65 / 0.97 | speckle on the dark water and the glitter band, −16% |
| linear 150/300, no floor | 81.8 / 5.60 / 2.13 | 77.2 / 9.60 / 5.04 | 72.1 / 2.95 / 1.23 | the deck's shadow on the water fills in, −28% |
| light cutoff 450 m alone | 77.4 / 4.63 / 0.89 | 78.7 / 3.61 / 1.55 | 62.8 / 9.41 / 3.59 | the far glitter band dims; Glitter loses a third of its streak band, −32% |
| light cutoff 300 m alone | 50.4 / 6.24 / 3.23 | 56.3 / 15.08 / 4.66 | 38.9 / 10.89 / 4.09 | the same, harder, −56% |
| linear 300/600 + cutoff 450 | 75.3 / 4.35 / 0.69 | 76.8 / 3.15 / 1.37 | 59.7 / 9.43 / 3.58 | the cutoff's change, with the gentle thinning's saving on top (lit arm; re-measure under borrow) |

**The owner's decision (2026-09-02, end of the session): a preset, not
dials.** `RenderSettings::RtOptimisation` — **RT optimisation** in the
panel, under the ray-tracing switch — with four levels, each a fixed row
of the table above (`RayOptimisationPresetFor` in RenderSettings.h is the
one place the numbers live):

| level | what it is | Headland, measured |
|---|---|---|
| Off | every ray traced, every light its range | 114 ms |
| Quality | linear 300/600, half the far rays kept at 450 m | 100 ms, nothing the eye finds |
| Balanced | linear 150/300, one ray in eight kept past it | 96 ms, speckle on dark water and glitter |
| Performance | no light reaches past 300 m | 50 ms, the far glitter dims |

Checked before the commit, Headland at 2560x1440, 150 frames, through
`--rt-optimisation=`: Off 114.4 ms, Quality 100.7, Balanced 96.0,
Performance 50.5 (busiest cluster 146 → 115 under the cutoff) — each level
lands on its matrix row.

The detailed fields are gone from the settings — a project carries a level
and cannot land on an untested combination — and `--shadow-rays=` /
`--light-cutoff=` remain as measurement overrides, with
`--rt-optimisation=<level>` to benchmark a preset without editing the
project. The project still ships at Off. Next: the eight-camera after-table
per preset (`bench_night.py --label after-<level> --extra
"--rt-optimisation=<level>"`) and the flicker protocol under all three AA
modes for whichever level the scene ships with (the dither is fixed per
pixel and walks only under TAA, by construction, but it has to be measured,
not assumed).

**Cost accounting the matrix settled.** Rays to far lamps are the
expensive rays: over the water toward a lamp half a kilometre away a ray
traverses most of the bridge, where a ray to the lamp overhead ends at
once. Keeping one in eight of them kept half their time (Headland 80 →
96 ms). So the frame-time lever is the *end* of the thinning and the
cutoff, and the floor is what the picture costs — the two are the same
dial seen from both sides.

**Traps.** A random draw instead of the dither blinks under no AA (WR-15's
first landing). The share pre-loop duplicates the loop's falloff
arithmetic on purpose, so the loop's own bits stay what they were. The
scene block grew a row (`ShadowRayFade`, appended) in all three mirrors.
`--frame-time=0` is the wall clock: pin with 0.000001 for the stills.

---

## WR-18 · The water's rays: one per quad, and a refraction ray that stops where the water has absorbed the answer — ✅ **built and measured 2026-09-02 (fourth session, late); in the presets**

**Owner-set, from the candidates list:** "implement both right now" — the
refraction ray that does not look where nothing can come back, and
reflections at half size. Two render-settings columns on the RT
optimisation preset (`RayRate`, `RefractionFloor` in
`RayOptimisationPresetFor`), two measurement flags (`--ray-rate=1|2`,
`--refraction-floor=<t>`), one scene-block row (`RayRates`, appended,
three mirrors).

**Why not a half-resolution reflection pass.** Two reasons, both
measured or structural. The reverted pass of 2026-08-27 read the normal
back from the screen buffer, which stores it in 8 bits per channel
(`kNormalFormat`), and reflecting doubles that error: never bit-identical,
and on grazing water the direction error is metres at the bridge. And the
water's normal — waves plus the detail map — exists only inside the water
shader; no pass can be handed it without a new buffer. The GI and AO
half-res paths get away with a denoiser because they are soft; a mirror is
not.

**Mechanism 1 — one ray per 2x2 quad, inside the water shader.**
`QuadTraceLane` / `QuadTraces` / `QuadShare` (`GL_KHR_shader_subgroup_quad`,
SPIR-V 1.5 target): one lane of each quad casts the mirror ray and the
refraction ray, the other three take the answer through a quad broadcast
— half size in each direction with the exact normal and no buffer. Under
TAA the tracing lane walks the four positions frame by frame so a still
quad fills back in; without a temporal filter it holds at lane 0 so the
picture stays put (WR-15's rule). Water only: an opaque quad can hold a
discarded lane (the thin-member fade) and a broadcast from a dead lane is
undefined; the steel's mirror rays stay per pixel. Every lane reaches the
broadcast (it sits outside the `mirror > 0` test) because a quad op inside
a branch some lanes skip is undefined.

**Mechanism 2 — the refraction ray's reach.** It looked 300 m. The
water's transmittance is `exp(-sigma * distance)`, so past
`-ln(floor) / sigma_min` the bottom, however lit, returns under `floor`
of its light: a 256th at the presets' value, 51 m in this bay
(GradientDepth 12 → sigma_min 0.108/m). The ray stops there, cheap whether
it hits or misses, and the open channel — where every ray used to travel
300 m to find nothing — is where the frame was paying. Off keeps 300 m.

**Presets:** Off 1 / none; Quality 1 / 256th; Balanced 2 / 256th;
Performance 2 / 256th.

**Results (2026-09-02, 150 frames per cell at 2560x1440; ms, then the
percentage of pixels moved by more than 2 and more than 6 levels against
the untouched still).**

| variant | Headland | Pier | Glitter |
|---|---|---|---|
| off | 114.0 / ref | 100.3 / ref | 107.3 / ref |
| refraction reach 1/256 alone | 101.4 / 0.00 / 0.00 | 97.3 / 0.00 / 0.00 | 87.3 / 0.00 / 0.00 |
| quad rays alone | 102.5 / 1.74 / 0.51 | 84.1 / 1.57 / 0.23 | 94.2 / 4.87 / 0.74 |
| both | 93.6 / 1.74 / 0.51 | 81.7 / 1.57 / 0.23 | 81.1 / 4.87 / 0.74 |
| **preset Quality** (thin 300/600 + reach) | **90.2 / 0.32 / 0.01** | **89.8 / 0.59 / 0.03** | **73.4 / 1.93 / 0.41** |
| preset Balanced (thin 150/300 floor 1/8 + quads + reach) | 74.4 / 4.86 / 1.97 | 69.8 / 5.64 / 1.90 | 59.2 / 7.11 / 1.73 |
| preset Performance (cutoff 300 + quads + reach) | 42.1 / 7.39 / 3.60 | 48.0 / 16.09 / 5.01 | 30.7 / 13.87 / 5.00 |

What it says. (1) **The refraction reach is a null on every camera** —
0.00% moved, maximum 2, 7 and 6 levels — and worth 13, 3 and 20 ms: the
open channel's rays were the cost, and Glitter, lowest to the water, gains
most. It is in every preset including Quality. (2) **The quad rays give
back a quarter of the water's ray time, not three quarters**: the three
idle lanes wait for the one that traces (SIMD lockstep), so a 4x cut in
rays is a 1.2–1.3x cut in time — 12, 16 and 13 ms. Their diff is the 2x2
sharing on the sharp parts of the reflection, 1.6–4.9% over 2 levels,
under 1% over 6; Balanced and Performance take them, Quality does not.
(3) **Quality is now 90 / 90 / 73 ms** against 114 / 100 / 107 with the
same diff it had — the lossless preset gained ten to twenty milliseconds.
(4) Performance is 42 ms on the hero camera, 2.7x the untouched frame.

**And the measurement that reorders the roadmap.** `--hit-lights=off`
(RayRates.z; a measurement flag, the picture is wrong on purpose) skips
the 191-light walk at every traced hit. Headland 114.2 → 74.4 ms (water
pass 74.8 → 39.8, opaque 37.4 → 32.5), Pier 100.4 → 57.6, Glitter 107.3
→ 68.3. **The light walk at reflection and refraction hits is about
40 ms — a third of the frame, more than the shadow rays were — and it is
a light-*read* cost, not a ray cost**: every hit reads all 191 light
records (80 bytes each, 15 KB a hit) with one range test each, and under
the bridge nearly every lamp is in range. WR-10 is the largest item on the
list by a wide margin; its design is in the ray-budget design document.

---

## Frame-time candidates beyond the list, and the GPU-driven LOD design doc read against this frame (2026-09-02, end of the fourth session)

The owner asked, with the frame still at 114 ms on Headland after WR-17's
presets, what else would buy time, and how the design document
`gpu_driven_on_the_fly_lod_raster_rt.md` (Downloads; a Nanite-shaped
clustered-geometry system with GPU LOD selection and separate LOD per ray
type) fits this engine. Written from the numbers, not from habit.

**Where the 114 ms goes** (Headland, 2560x1440, RTX 5070 Ti laptop; the
1600x900 isolation scaled by pixel count, so the shares overlap and sum
past 100): shadow rays ~38 ms (WR-17's presets take 14–64 of it), the
water's refraction rays ~33, reflection rays ~23 (water 15, steel 8), the
lit loop's own BRDF walk ~35 (146 lights of anisotropic Beckmann per water
pixel ~17, the steel ~14). Everything else in the frame is under 2 ms
together. Geometry is not the cost: 2.85 M triangles, 200 draws, 1 ms of
CPU, a depth prepass already in. The frame is fragment- and ray-bound.

### The design document, read against this frame

**What it proposes.** Meshes cut into ~128-triangle clusters with a
hierarchy and a geometric error per level; GPU compute selects clusters
per frame by screen-space error, frustum, Hi-Z occlusion and temporal
hysteresis, then emits indirect draws; ray tracing selects its *own* LOD
per ray type by importance (reflections coarser than primary, GI coarser
still, shadow rays by blocker silhouette), first as prebuilt BLAS levels
per mesh, later as cluster-level RT geometry; then geometry paging and
streaming; only last, on-the-fly GPU simplification, cached, never per
frame. Seven phases.

**What the engine already has.** GPU culling into indirect draws (roadmap
8.3, `cull_lit.rvshader`), a meshlet lit stage (`pbr_meshlet.rvshader`),
the depth prepass, a per-instance TLAS with a caster table
(`RayShadows`), and instance masks written into the TLAS
(`VulkanResources.cpp`, `out.mask`) — which every ray query then ignores by
passing `0xFF`. It has no LOD chain and no simplifier (NEXT row 5), no
Hi-Z occlusion, no cluster hierarchy.

**The raster half buys almost no frame time here.** The geometry is
cheap; the Scene pass's 38 ms is lighting and rays per fragment. What
raster LOD buys this scene is *quality*: the sub-pixel pickets, ropes and
truss webs that flicker under TAA (WR-13's residual) are exactly what a
distance LOD removes. That stays NEXT row 5, and the document's Phase 1–2
is the right shape for it when it comes; Phase 3's Hi-Z is worth little on
an open bridge.

**The RT half is the fit, and it lands on WR-17's residual.** The matrix
proved two things the document's Phase 4 is built for: the far visible
shadow on the water is the deck, the towers and the piers — big blockers —
and far rays are expensive because they traverse the bridge's thin steel
(one in eight kept half their time). Phase 4's "Strategy A" collapses, for
this scene, to something small:

1. **A proxy BLAS per bridge mesh** — the deck as a slab, the towers and
   piers as boxes, the railings as a strip — that the generator can emit
   beside the real meshes (it already writes both). Added to the *same*
   TLAS through `AddInstance` with instance mask bit 1; the real geometry
   keeps bit 0.
2. **Far shadow rays trace with the proxy mask.** `TraceShadowSoftFrom`
   already knows the light's distance and WR-17's skip fraction; the same
   rule picks the mask: near lights against the real structure (the
   pickets' penumbra, which is the look), far lights against the proxy.
   The bake and every other query keep bit 0.
3. Later, a mid-detail BLAS for the water's mirror rays (a tower's
   reflection does not need pickets) and the refraction rays.

What it buys: far rays a fraction of their cost, so WR-17's floor stops
being a quality dial — all far rays traced, against the proxy, for about
what one in eight costs today — and Balanced's speckle and the deck's
shadow filling in go away. The number is an estimate to be measured; the
mechanism is not. One to two days. Skip for now: Strategy B (cluster-level
RT needs custom traversal), phases 5–7 (2.85 M triangles fit in memory;
the document's own rule is never to simplify per frame). Its baked-GI
caveat holds by construction: the solve traces its own path against the
full structure.

### In plain words — the owner's list, noted as read (2026-09-02)

1. **Do not look into the water where you cannot see into it.** Every
   water pixel currently fires a ray under the surface. At a low viewing
   angle, or where the water is deep, nothing comes back. Skip those rays.
   Could be worth 30 ms.
2. **Render smaller and let the anti-aliasing fill it back in.** Render at
   three quarters size, upscale with the TAA we already have. Cuts most of
   the frame by a third or more. The glitter might soften. The owner's eye
   decides.
3. **Do reflections at half size in their own pass.** About 7 to 10 ms. We
   tried once and hit a precision problem; it is a one-line fix.
4. **WR-10 matters more than we thought.** The reflection and under-water
   rays each light their hit with all 191 lamps. Already on the list, just
   bigger.
5. **Smaller wins:** skip lights too dim to see, and shade smooth dark
   water at a lower rate.

The detail behind each is the ranked list below.

### Other candidates, ranked by expected time on the sea cameras

1. **Refraction rays only where transmission can show** (up to ~30 ms).
   Every water fragment traces a refracted ray 300 m into the scene, then
   shades the hit against all 191 lights and adds caustics
   (`pbr_fragment.glsl`, `RV_RAY_REFRACTION`). At grazing angles Fresnel
   sends almost nothing into the water, and the transmittance
   `exp(-sigma * through)` is under 1/256 a few metres down. Gate the ray
   on `(1 - F) * exp(-sigma_min * waterThickness)` above a threshold —
   `waterThickness` is already read from the backdrop depth before the
   block — and otherwise take the lit colour, which is what a miss already
   does. Every engine's water skips refraction at grazing on the Fresnel
   term; the diff image will show exactly where it changes.
2. **Render scale with temporal upsampling** (30–45% of everything
   per-pixel). No render scale exists (SSAA only goes up). The frame is
   per-pixel bound, TAA is in, and a 0.75x internal resolution resolved up
   by the TAA is the standard lever (TAAU). Risk: the glitter's sharpness
   under upsample; the owner's eye decides.
3. **Reflection rays out of the fragment shader, at half resolution**
   (~7–10 ms). Reflections still ride in the lit shader at the frame's
   pixel rate; the reverted pass failed on the 8-bit normal buffer
   (`kNormalFormat`, one constant) — R16G16 octahedral unblocks it, and
   RTGI's own Medium rung proved half-res rays indistinguishable at 12x.
   With it, a night-specific cut for the steel's mirror rays: at roughness
   0.27 the whole bridge traces a ray per pixel to see dark sky and water.
4. **The proxy BLAS above.**
5. **WR-10's true size.** Not new, but both the reflection and the
   refraction hits walk all 191 lights with no cluster, and 56 ms sits in
   those two passes. After WR-17 it is the largest listed item.
6. **Contribution culling in the lit loop** (~5–8 ms, a candidate). The
   water evaluates 146 lights' full streak BRDF; a light whose unshadowed
   term is under 1/512 of the pixel's last-frame luminance cannot show.
   Needs the TAA history bound in the lit pass — one sampler slot is left
   on OpenGL's layered variant.
7. **Variable-rate shading.** The 5070 Ti supports it; the engine queries
   nothing for it. The ray budget's importance tiles are a ready-made
   shading-rate image: 2x2 on smooth dark water, 1x1 on the glitter and
   the steel. Medium effort, untested here.
8. **Async overlap** of the ray passes with raster, once (3) exists. Small.

Not worth touching: RTAO (0.1 ms), the post chain (under 2 ms together),
MSAA and SSAA (inert under TAA), Hi-Z occlusion (an open structure).

### The second document, `RT_Multi_Light_Shadow_Optimization_Debugging.md`, read against what is measured

A profiling-and-optimisation checklist for many shadow-casting lights,
written for exactly this case (191 lights, 147 casting, a 90 ms frame, a
per-light distance falloff already in). In plain words:

- **Most of its diagnostics are done.** Shadows on/off (about a third of
  the frame), light count per pixel (the busiest cluster holds 146–178),
  rays per light (one, WR-15), the per-light falloff (WR-17, four arms
  measured), resolution scaling (the frame scales with pixel count, so it
  is pixel- and ray-bound), and acceptance by per-pixel diff (the standing
  rule; it suggests PSNR/SSIM, which the diff-image rule already beats).
- **Its one big recommendation is WR-16.** Replace the per-light falloff
  with a *global per-pixel ray budget*: rank the pixel's lights by
  importance, pick a few at random in proportion, trace only those, weight
  by the odds, and let temporal reuse fill in the rest over frames. That is
  what ReSTIR DI does with reservoirs and spatial reuse on top. The
  document argues to do it; our matrix adds the caution it lacks: tracing
  far lamps sparsely per frame reconstructs their shadow only with a
  temporal filter, and the owner's rule is that the fix must hold under
  every AA mode. ReSTIR's spatial reuse is what makes it work without
  leaning on TAA alone — the plain "budget + temporal" form would be the
  no-AA blink WR-15 already taught us.
- **One of its assumptions is contradicted by the matrix.** "Skip the
  shadow ray when the light's contribution is negligible" is the Share
  shape, and it lost: a hundred negligible lamps behind one slab are not
  negligible together. Any per-cluster cap on casting lights ranked by
  contribution has the same failure built in.
- **Worth taking now, cheap:** a rays-per-pixel and lights-per-pixel debug
  view and a total-shadow-ray line in the benchmark report (today only
  "busiest cluster" is printed). Its ray-mask and shadow-LOD advice is the
  proxy-BLAS item above. Its hybrid idea — shadow maps for static lights —
  is an option for the 128 fixed lamps (render each map once and keep it;
  the sea is a receiver, not a caster), with the penumbra quality and the
  memory to be judged; noted, not recommended yet.
- **Before WR-16 is built, run its Experiment 4 as a pre-check:** a fixed
  budget of 1, 2, 4 and 8 shadow rays per pixel spread over the cell's
  lights by importance, under no AA, MSAA and TAA, on the three cameras
  with the matrix's diff. That is a day, and it says what any budgeted
  scheme can reach here before three weeks go into reservoirs.

---

## Deferred / rejected, with reasons (do not re-litigate blind)

- **ReSTIR GI** — replaces the whole GI estimator; heavy, denoiser-entangled.
  Revisit only after WR-10's ladder is exhausted. (Full deep-dive summary in
  WR-10's options; primary sources: Bitterli 2020, SIGGRAPH 2023 course.)
  **Still rejected, and for the unchanged reason.**
- ~~**ReSTIR DI**~~ — **no longer deferred: it is WR-16**, owner-set
  2026-09-02. This list previously held it on the grounds that at N≈150
  homogeneous lamps the reservoir machinery buys little over a grid+CDF.
  That reasoning was sound on the evidence available and is superseded by
  evidence that arrived later: RT shadows measured as the single biggest
  item in the night frame (~15 ms of ~50), caused by every one of the 128
  lamps casting with no cap. WR-16 carries the design; this row stays so the
  change of mind is legible rather than looking like the list simply forgot.
- **Light BVH / stochastic lightcuts** — built for thousands-to-millions of
  emitters; at N≈150 the grid+CDF wins on simplicity (RTSL: 11.5 ms at
  1080p/RTX 2080 — an order of magnitude more machinery than this needs).
- **Full Hillaire sky / Jensen night-sky model** — overkill for one fixed
  night scene; the shaped gradient + glow lobe reproduces the photographs.
- **Runtime FFT/convolution bloom** — the bright-source list is known and
  short; sprite PSF splats beat it on cost and TAA stability.
- **GT7 dual-timescale adaptation** — pays off with camera cuts/day-night
  cycles; the fixed night exposure is its correct degenerate case today.
- **Acuity blur (scotopic)** — fights TAA and the GI denoiser; skip.
- **Uniform "dark-scene GI boost"** (the found design doc's §11) — a gain on
  indirect keyed to a noisy per-frame darkness signal; rejected on the
  owner's physically-correct ruling and the no-frame-varying-parameters rule.
  The *symptoms* it targeted are addressed causally: information-starved
  darks get sky energy (WR-1), light-in-fog (WR-11), sampling (WR-10),
  glare (WR-5).
- **Purkinje ON by default** — wrong for the long-exposure target (§0).
  The research strands genuinely disagreed here (one ranked it a top-3 build
  item; the display strand said off-for-photograph); §0's choice follows
  from the owner's own validation method — a Purkinje-desaturated render
  diffs badly against saturated long-exposure references *by construction*.
  The toggle (WR-12b) keeps the choice demonstrable and reversible.
- **SSR for lamp reflections** — resolved to traced-only (WR-9's contract);
  SSR's off-screen-source failure is maximally visible with 128 lamps.
- **HDR display output now** — judged SDR-first (§0); the per-profile
  parameter-set structure is the only concession made today.
- **A froxel-depth caution, not an item:** at 2.7 km scene scale, froxel
  slice distribution, height-fog depth and traced shadows at range can
  surface banding/precision artefacts that will get misattributed to the
  night stack. Reverse-Z already shipped (NEXT.md row 2) — if far-field
  banding appears, suspect the froxel slice mapping first, not depth.

---

## Current-state evidence (for whoever picks this up cold)

- Captures: `build/night-research/night_{glitter,headland,deck,pier,cliff}.png`
  (2560×1600, committed scene, bake-matched, frame 240).
- Research notes with full source URLs: this file's items each carry their
  keystone sources inline; the raw four-strand research dump and the ReSTIR
  deep-dive live in the session scratchpad and the key claims are reproduced
  here — this file is self-sufficient.
- Reference photographs studied (Wikimedia Commons, full-res): "Fort Point
  National Historic Site and Golden Gate Bridge" (Brocken Inaglory),
  "Night shot of Golden Gate Bridge and San Francisco" (fog bank),
  "Golden Gate Bridge by night" (Marin-side span view).
- Keystone published sources: Real-Time Samurai Cinema (SIGGRAPH 2021,
  glowybits.com — sky/probes/froxels/local-tonemap/Purkinje with constants);
  GT SPORT HDR course (SIGGRAPH Asia 2018) + GT7 "Driving Toward Reality"
  (SIGGRAPH 2025, blog.selfshadow.com — tonemap, exposure, F-number glare);
  Heitz et al. LTC (SIGGRAPH 2016) + Heitz & Hill line/tube (2017);
  Wronski AC4 froxels (GDC 2014) + Hillaire Frostbite volumetrics
  (SIGGRAPH 2015); Jimenez CoD:AW post (SIGGRAPH 2014); Spencer et al.
  glare (SIGGRAPH 1995); Gjoel & Svendsen INSIDE dither (GDC 2016);
  PNNL-21894 (DOE GATEWAY, the bridge's measured photometry); Karis UE4
  course notes 2013 (representative point); Metro Exodus Enhanced technical
  deep-dive (4A); Unity HDRP RT Light Cluster docs; Ray Tracing Gems II
  ch. 23 (ReGIR); Bitterli et al. ReSTIR (SIGGRAPH 2020).
