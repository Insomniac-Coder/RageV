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
| WR-3 | Fog inscatter takes the sky's colour per view direction | BOTH | ~0 | 0.5–1 d | WR-1 |
| WR-4 | Photometry & fixtures pass (scene data) | BOTH | ~0 | 1 d | WR-0 |
| WR-5 | Night glare: thresholdless audit, PSF weights, lamp flare sprites | BOTH | <0.5 ms (inferred — run the overdraw benchmark in the item) | 2–3 d | WR-4 helps |
| WR-6 | Water sparkle: local specular bound replaces flat ReflectionFloor | VK-only | ~0 | 1 d | — |
| WR-7 | Capsule (source-length) specular — interim streak fix | BOTH | ~0 | 1 d | — |
| WR-8 | LTC area lights: rect, then line/tube for the lamp rows | BOTH | ~3–5× punctual ALU per light, cluster-bounded (measured class: 0.56 ms/full-screen light on a 2014 laptop GPU, near-linear) | 4–6 d | WR-13 with it |
| WR-9 | Luminaire binding + the single-counting contract | BOTH (flags), VK-only (ray-hit tags) | 0 | 1–2 d | WR-8 |
| WR-10 | World-space light grid + stochastic sampling at RT hits | VK-only | net ≤0, build <0.5 ms | 3–4 d | — |
| WR-11 | Froxel volumetrics (or analytic airlight fallback) | GL-fallback (see item) | 1–1.5 ms (shipped-precedent class) | 1–2 wk | WR-1, WR-4 |
| WR-12 | Display finishers: night toe, Purkinje switch, local exposure, stars | BOTH | <0.5 ms | 2–3 d | WR-1, WR-5 |
| WR-13 | Specular antialiasing (Tokuyoshi–Kaplanyan) — lands WITH WR-8 | BOTH | few ALU/lit px | 1 d | — |
| WR-14 | Water foam at night | BOTH | ~0 | 0.5–1 d | WR-1, WR-4 |
| WR-15 | Soft shadows from dense point-light arrays (tower-base streak fan) | VK-only | untested | 3–5 d (design first) | WR-8 helps |

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

## WR-3 · Fog inscatter takes the sky's colour per view direction

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

## WR-4 · Photometry & fixtures pass (scene data only, no engine code)

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

## WR-5 · Night glare: bloom audit, PSF weights, lamp flare sprites

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

## Deferred / rejected, with reasons (do not re-litigate blind)

- **ReSTIR GI** — replaces the whole GI estimator; heavy, denoiser-entangled.
  Revisit only after WR-10's ladder is exhausted. (Full deep-dive summary in
  WR-10's options; primary sources: Bitterli 2020, SIGGRAPH 2023 course.)
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
