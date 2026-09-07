"""RT-first T5: the design, written into docs/RT-FIRST.md as section 2d
before anything is built (owner's rule: propose architectural changes
before building), and the ray-budget consequences the owner asked to be
told about."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

p = 'docs/RT-FIRST.md'; s, nl = load(p)
anchor = '## 3 · What this does to the open lists'
assert s.count(anchor) == 1
design = """## 2d · T5 — Direct light as a signal on the contract (design, 2026-09-06; owner said "go for T5, if this affects ray budgeting then let me know")

**What the engine does today, measured on the garage (1600x900, Quality, T4 build):** the lit fragment shader walks the cluster cell's light list -- 27 lights per pixel on average, 30 in the scene, 23 casting -- and traces **one soft-shadow ray per casting light per pixel**: 24.47 M shadow rays a frame, 17 per lit fragment, and `scene/Scene` at 4.84 ms is the largest pass of the 11.6 ms frame. Nothing reconstructs the direct light temporally; only TAA integrates the per-frame disc sample, which is why a moving camera shows the wall's motion noise (the TAA history is invalid exactly where the disc sample changes). The bridge is the same shape at 78 lights and ~15 ms. WR-17 thins far lamps by distance (a no-op at garage scale); S1's `--shadow-budget=K` and S4's `--light-sampling=K` sampler exist inside the loop but the sampler engages only where the irradiance field owes nothing (`fieldWeight <= 0`) and the cell holds more than 2K lamps -- off on the garage floor, which sits under its volume.

**The precedent already in the engine:** WR-16 S4 built exactly this architecture for the sea -- a surface prepass, `water_choose` (K reservoirs per pixel by weighted reservoir sampling on a cheap target), `water_shade` (the K survivors shaded in full, one shadow ray each), `water_accumulate` (the *light* averaged over frames, not the choice -- the choice reuse was measured to lose), and the water draw adding two textures instead of walking the lamps. T5 is that pipeline for every opaque pixel, fed by T1's G-buffer and denoised by T4's contract instead of the water's own accumulate.

### The passes (between "GBuffer" and "Scene")

1. **`DirectTrace`** (fullscreen, `RV_TRACE_ONLY` over the lit shader as `rtgi_trace` and `water_shade` do; reads the G-buffer: depth, `o_Surface` = oct normal + roughness + metallic, `o_Albedo` = albedo + specular scalar, `o_SurfaceId` whose **sign carries the Static flag**). Per pixel: reconstruct the surface (`view_reconstruction.glsl`), find its cluster cell (a `ClusterCellAt(worldPos, uv)` that needs no varyings), walk the cell's 16-byte cull records (`LightCullRejects`), and **keep K lights by weighted reservoir sampling** on S4's target 1 -- the unshadowed irradiance times the BRDF's magnitude (diffuse albedo luminance over pi plus the GGX lobe's peak; the sampler's own score at the lit shader's line ~4545). The **directional lights are always shaded** (there are at most a few; they are not in the cell). Each survivor: the full BRDF exactly as the lit loop computes it (kD, Fresnel, Smith), `liveShare` for the baked-field split (`BakedShare`, the same function), **one soft shadow ray** (`TraceShadowSoftFromMasked`, the light's `kind`/CastShadows exemption kept -- the lesson of S4b), its light divided by its selection probability over K. Choose and shade are **one pass, in registers** -- no reservoir textures, no neighbour pass; the temporal/spatial reuse of choices is T10's (ReSTIR DI), and on the water it measured as a loss. Two outputs, R16G16B16A16_SFLOAT:
   - `DirectDiffuse`: sum of kD x radiance x cos x visibility x liveShare -- **the albedo demodulated** (the lit shader multiplies by albedo / pi), so the denoiser blurs light and never texture;
   - `DirectSpecular`: sum of specular x NdotL x radiance x visibility x liveShare, complete.
   - `.a` = 1 on a lit surface, -1 on sky/empty (the contract's "no surface" flag).
2. **`DirectAccumulate` + `DirectBlur` x3**: T4's contract with `SignalParams{ Diffuse, Slot 1 }` -- surface reprojection, the normal/plane/roughness tests, the silhouette rule, the motion-capped memory, the mean +- 3 sd bound (a switched light falls outside the bound everywhere and the history drops in one frame, which is R9's 47-level lag answered structurally), the young-history blur bounded by `MaxRadius`. **The contract gains a second payload** (`RV_SIGNAL_PAIR`: a fourth accumulate attachment, a second blur attachment, `fresh2`/`history2` inputs) so the diffuse and specular halves share one set of surface tests, one refusal reason and one blur radius; the reflection instance passes null and compiles as before. One memory for both halves in the first landing; a per-payload memory (the water's glint needed a short one) only if the highlights smear under camera motion -- measured, not assumed.
3. **The lit pass consumes it.** Under a new define `RV_DIRECT_SIGNAL` (pushed with `RV_RAY_SHADOWS` when the signal runs) the opaque lit shader's light loop keeps **only the subtractive branch** -- a static surface under a fully baked lamp with a moving object in range traces its one moving-only ray and the loss is clamped against the field's stored direct light exactly as today, because `storedDirect` lives in the lit shader and the case is rare and cheap -- and adds the two textures: `Lo += diffuseTex * albedo / PI + specularTex` (bound at set 0, bindings 26 and 27). The `directIrradiance` pre-loop, WR-17's thinning, S1's reservoirs and S4's in-loop sampler all fold away under the define. The water keeps its own passes (they are this design already); transparent surfaces keep the in-shader loop (they are not in the G-buffer).

### What it costs and what it should give (estimates, to be measured)

Garage at K = 8 (Quality's `Lamps`): 1.44 M x 8 = 11.5 M shadow rays against 24.47 M; at K = 4, 5.8 M. The trace pass pays the cheap walk plus K BRDFs plus K rays; the accumulate and three blurs cost what the reflection's do (0.27 + 0.39 ms). The Scene pass loses its light loop -- its largest loop, and the register pressure that comes with it. Expected: the frame's shadow work roughly halves at K = 8, and **the direct light has a temporal reconstruction for the first time**, which is the wall's motion noise. The bridge: K = 8 against 9.3 rays and 78 lights per fragment today.

### Ray budgeting -- what T5 changes (the owner's question)

1. **Shadow rays stop being one per light and become K per pixel.** The count is bounded and predictable for the first time -- 1.44 M x K -- which is the "shadow spender" WR-16's design named (Part IV, S4 "the proper way"). K is the preset's existing `Lamps` field (Quality 8, Balanced 4, Performance 2, Off 0 = every light, the old loop) and the existing `--light-sampling=K` override; **no new dial** (owner's rule: global render settings). The water reads the same K.
2. **WR-17's distance thinning, S1's `--shadow-budget` and S4's `--shade-lights` become dead for opaque surfaces.** The sampler thins by contribution, which is the "Share" shape the owner's own rule called the physical form. They stay in the code under the old path (`--direct-signal=off`), which is the A/B and the truth arm.
3. **The per-tile K lane in the allocator (S3's widening: "the shadow-ray ceiling, S4's K per tile") is not built in T5.** Part IV decision H's reasoning holds: build the consumer first, then size the lane for it. The trace pass reads the tile budget map like the GI trace does, so a per-tile K is one lane and one line when S3 is widened (T10's neighbourhood). Until then K is per preset.

### What is knowingly approximated (to check on the bridge)

- The specular scalar rides `o_Albedo.a` (metallic is already in `o_Surface.a`, so nothing is lost); the coat's diffuse wrap (`surface.Coat.w`) is not in the G-buffer and the signal path shades it unwrapped -- the loop path keeps it. A G-buffer lane for it is a T8 question if a scene shows it.
- The material's own occlusion map does not enter the subtractive clamp's bound in the pass (it stays in the lit shader, where the clamp still runs), so nothing changes there.

### Verification (the protocol)

1. **Truth:** the old path (`--direct-signal=off`, every casting light traced, TAA) converged still against the new path converged still, garage at the owner's camera: a per-pixel diff image with no structure beyond grain (owner's rule: diff images, not means); mean under a level. An unbiased K-sampler must converge to the same picture.
2. **The wall's motion noise:** the reflections-off dolly arm's per-frame wall change (5.5 today) toward the parked floor -- this is the verify RT-FIRST §2 step 2 named for shadows.
3. Frame time and `rays per frame` (24.47 M shadow rays -> ~11.5 M at K = 8); `parked_stats`, `edge_shake`, `smear_metric` on the reflection arms unchanged or better.
4. **Light switch:** the Switcher scene's settle after a switch against the base renderer's 47-level lag (R9).
5. **The bridge, three cameras:** the diff against the old path, frame time, and the car under a baked lamp still casting (the subtractive path).
6. Debug views: `direct-light` (the accumulated pair), `direct-refusal` (the contract's), both on the ramp.

"""
s = s.replace(anchor, design.replace('\n', nl) + anchor)
s = s.replace("| T5 | Shadows on the contract: one visibility ray per pixel toward the chosen light, the ratio form, the denoiser, the lit shader reading the result instead of tracing (the wall's motion noise; the bridge's ~15 ms) | 2b | large | |",
              "| T5 | Shadows on the contract: one visibility ray per pixel toward the chosen light, the ratio form, the denoiser, the lit shader reading the result instead of tracing (the wall's motion noise; the bridge's ~15 ms) | 2b | large | 🔨 IN PROGRESS 2026-09-06: design in §2d (direct light as a signal: one trace pass choosing K lights per pixel, the T4 contract with a second payload, the lit shader adding two textures); building |")
save(p, s)
print('T5 design written')
