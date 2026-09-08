import io, sys

P = r'docs/HANDOFF.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if '2026-09-08' in s.split('## ')[1][:200]:
    sys.exit('already written')

old_header = s[s.index('**Read this first.**'):s.index('**Superseded header.**')]
new_header = '''**Read this first.** Updated 2026-09-08: **the fifteenth entry below is the current
hand-off** -- RT-15 (both halves), RT-19 and RT-7's capsule are done and measured, RT-8 is
part done, and RT-17/RT-18/RT-19 were filed from the owner's OATR document. `docs/RT-SERIES.md`
opens with a status table (20 of 33 closed) and is the one list; read it before picking
anything up. **The thing to pick up first is one measurement, not a build:** RT-8's third job
is argued against on a claim that was never verified -- see the entry. Older headers follow.

'''

new_entry = '''## 2026-09-08: the motion nothing recorded, the counters that would have found it, capsule lights, and the sea's own layer

**State.** Everything below is committed on `main`. Both builds and both staged shader
folders are current. The garage renders bit-identical to the previous day throughout; the
bridge's sea is measurably smoother.

### RT-15 -- ✅ both halves, and the root cause was not in the shader

**An object moved by a fixed-step script had no motion vector at all.**
`Scene::AdvanceMotionHistory` copied `World` into `PreviousWorld` at the top of
`OnUpdateRuntime`, but Application steps the fixed scripts *before* the layers update and
`OnFixedUpdateRuntime` ends by deriving the world transforms -- so the copy captured the
position the object had just moved to. Where it was and where it is were the same matrix,
and the velocity lane read exactly 0.000 while the object crossed the screen. **Physics and
per-frame scripts were never affected**; it was the tick rate, which is the main script
rate. Fixed by taking the snapshot once a frame, before anything moves, by whichever update
reaches the frame first.

Then the shader half: the reflection accumulator reprojects by object motion
(`ObjectShift`), and the *image* by the mirror rule (`ImageThen` -- keep the part of the
motion along the normal, double it; the general two-reflection form also handles a reflector
that turns). **Panel flicker 15.20 -> 10.96 (engine) -> 4.39 (plus shader).** Parked garage
and the camera dolly both **bit-identical**.

**Three holes, all the same mistake -- assuming a quantity is exactly zero when nothing
moves.** The UI shaders write `o_Velocity = 0` unconditionally, so the credit strip
disagreed by up to 3.9 texels; the velocity lane and the matrix are computed differently so
they agree only to rounding (hence `kMotionFloor = 0.125` texels, set from measurement); and
the stored plane distance is half precision, so a still reflector did not return its image
bit-exactly. **And `ImageThen` may only use the texel's own history** -- a neighbour's plane
is a different point of the surface.

### RT-19 -- ✅ the refusal reasons, totalled

The ray-counter block went 16 -> 32 lanes. Four lines now print beside the ray counters:
acceptance rate and average history depth for the temporal resolve and for the reflection
accumulator, and both split by which test refused. **No plumbing was needed** -- the counter
buffer was already declared at set 0 binding 21 and already bound to the accumulate pass.

**What it says, and it is a finding:** on the garage with a near-mirror crossing the frame,
the reflection accumulator refuses **essentially nothing** and holds **fifty frames**. The
smearing on a moving reflector is not a shortage of refusals.

### RT-7 -- 🔨 the capsule half

`Light::SourceLength` in metres and the Karis representative-point capsule in the analytic
specular. **Null test bit-identical at length 0.** Garage tubes at 2.4 m: car body 7.1% of
pixels changed, upper walls 5.0%, ceiling 2.3%, up to 60 levels.

**The owner's ruling on orientation:** the length is a parameter on the light and the
direction comes from the **transform** -- rotate the fitting to turn the tube, as you rotate
a spot to aim it. The axis is the light's local X and the inspector tooltip says so.
Verified: rolling the tube lights 90 degrees changes 2.56% of pixels by up to 47 levels.
**Reading the axis off the emissive bar mesh was proposed and rejected -- do not re-propose.**

`GpuLight` is now **96 bytes**, not 80, with a `vec4 Extent` (x length, yzw the axis). The
zero-byte encoding it started with rode `Params.z`, free on everything but a spot -- and the
garage's tubes *are* spots. **The 16 bytes are unmeasured on the bridge; that is owed.**

**What is left: the wet floor barely moved (2 levels).** Mirror-like surfaces take their
tube reflections from traced rays, not the analytic highlight, so the capsule reaches rough
surfaces only. That is the other half of RT-7 and it overlaps RT-11.

### RT-8 -- 🔨 the water's own layer, and a finding that reordered the item

The sea's surface pass now carries **the wave's own motion, the mask and the object id**.
The motion was never missing -- `water_vertex.glsl` has always evaluated the wave at last
frame's time -- there was nowhere to write it.

**The bug that hid it:** `Renderer3D`'s water surface pipeline hard-codes `ColorFormats` and
`BlendPerAttachment` with **three** entries. The target grew a fourth and the pipeline did
not, so the shader's write to location 3 went **nowhere, in silence**. `--debug-view=water-mask`
showed the layer empty where the sea plainly was, and that was the whole diagnosis. **A
pipeline carries its own attachment count; growing a target is never enough.**

**The finding: the sea reading zero velocity was load-bearing.** Handing the temporal resolve
the wave's true motion made the water *specklier* -- 1.160 -> 1.432 at the pier, and 1.503
when the motion drove only the stillness test. A wave carries glitter, which is not attached
to the water, and `TemporalStillFeedback = 0.98` was averaging that sparkle over ~50 frames
**because** the seabed's velocity under the sea said nothing had moved.

**Resolved by giving the sea an average of its own.** The water accumulate keeps the lamp
glint on a separate memory and it was **two frames**. At the pier with the motion live:
1.432 at two, 1.019 at eight, **0.912 at sixteen**, 0.829 at sixty-four -- against **1.160**
shipped. Not haze: contrast rises (sd 17.28 -> 18.46) and peaks brighten. **Landed at 16.**
Final: **pier 1.160 -> 0.915, glitter 0.653 -> 0.591**, deck and garage bit-identical.

Also: the water accumulate now reprojects by the wave rather than the previous camera alone.
Measured **neutral** on both static cameras; kept because it is the quantity that describes
what moved. It wants a bridge camera dolly to judge, which no harness here has.

**New views: `--debug-view=water-motion` and `--debug-view=water-mask`.**

### What is open in RT-8, in the owner's own words

The sea keeps its own copy of three machines the rest of the engine shares:

1. **Lighting** -- works out how bright the surface is. Move it onto the shared pass. Not
   done: the shared pass knows one highlight shape and the sea's stretches along the wind.
2. **Ray clean-up** -- smooths the noisy reflection and refraction rays across neighbours.
   Move it onto the shared one. Not done; depends on 3.
3. **Frame averaging** -- blends this frame with previous frames. **Argued against, and the
   argument is NOT yet verified.** The shared version only reuses last frame's value when the
   surface is the same distance away, and a passing wave slides the point seen at a pixel by
   tens of metres. Measured: switching the sea's averaging off costs **1.160 -> 1.609**. But
   that measures *the sea with no averaging*, not the sea under the shared gate -- **nobody
   has measured how often that gate would actually refuse a water pixel.** RT-19's counters
   make it cheap. **Do that measurement before building or dropping job 3.**

### Traps paid for today

- **`half` is a reserved word in GLSL.** The shader failed to compile, the pipeline came back
  null, and the crash surfaced in `Scene::RenderShadowMaps` -- nowhere near the edit.
- **A pipeline's attachment count is its own** (above).
- **The runtime must be run from `build/bin/Release/RageVRuntime`.** From the repo root it
  silently compiles no shaders and every frame comes back black, which reads as a broken
  change.
- **The counter stride is in four places** -- `RayCounters::Count` and
  `RayCounterSlot() * 32u` in `pbr_fragment.glsl`, `taa_resolve` and `rtao_compute`.
- **Prove a slot before suspecting the data**: binding the G-buffer normals to the resolve's
  new texture slot showed the plumbing was good and the attachment was empty.

'''

s = s.replace(old_header, new_header, 1)
anchor = '## 2026-09-07, end of day:'
i = s.index(anchor)
s = s[:i] + new_entry + s[i:]
io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('hand-off updated')
