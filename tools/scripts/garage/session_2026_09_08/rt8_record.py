import io, sys

P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'RT-8 — 🔨' in s:
    sys.exit('already recorded')

old = '| RT-8 | open | 4-6 d | **high** | the water on the G-buffer |'
new = '| RT-8 | 🔨 **part done 2026-09-08** — the layer and its motion; the light and the rays open | 2-4 d left | **high** | the water on the G-buffer |'
if s.count(old) != 1:
    sys.exit('status row matched %d' % s.count(old))
s = s.replace(old, new, 1)

record = '''### RT-8 — 🔨 part done 2026-09-08 (uncommitted)

**What the sea now has.** Its surface pass was already a G-buffer of its own in
all but name -- position in full floats with a mask, the normal with roughness
and wind, the colour with the specular dial. It now also carries **the wave's own
screen motion and the object id**, in a fourth attachment. The motion was never
missing: `water_vertex.glsl` has always evaluated the wave at last frame's time
and built `v_PrevClipPos` from it, with a comment saying why. There was simply
nowhere to write the difference.

**The bug that hid it, and the class to check first.** `Renderer3D`'s water
surface pipeline hard-codes `surface.ColorFormats` and `BlendPerAttachment` with
**three** entries. The target grew a fourth attachment and the pipeline did not,
so the shader's write to location 3 went **nowhere, in silence** -- no validation
message, no warning. `--debug-view=water-mask` (new, below) showed the layer
empty where the sea plainly was, and that was the whole diagnosis. **A pipeline
carries its own attachment count; growing a target is never enough.**

**The finding that reordered the item: the sea reading zero velocity was
load-bearing.** Handing the temporal resolve the wave's true motion made the
water *specklier* -- 0.653 → 0.763 at the glitter camera, 1.160 → 1.432 at the
pier, and **0.787 / 1.503** when the motion drove only the stillness test. The
owner saw it before the metric did. A wave carries glitter, which is not attached
to the water, and `TemporalStillFeedback = 0.98` was averaging that sparkle over
about fifty frames **because** the seabed's velocity under the sea said nothing
had moved.

**Resolved by giving the sea an average of its own instead of a borrowed one.**
The water accumulate already keeps the lamp light's glint on a separate memory
and it was **two frames** -- short for a good reason at the time, since the sea
also had TAA's fifty behind it. At the pier with the motion live: speckle 1.432 at
two, 1.019 at eight, **0.912 at sixteen**, 0.829 at sixty-four, against **1.160**
for the shipped build with no water motion at all. **Not haze:** contrast rises
(sd 17.28 → 18.46) and the peaks brighten (99.9th percentile 151.8 → 155.2) all
the way up the sweep. Landed at sixteen -- the curve is nearly flat past it and a
shorter memory follows a light that goes out sooner.

**Measured, final:** pier speckle **1.160 → 0.915**, glitter **0.653 → 0.591**,
the deck bit-identical, the garage bit-identical. The sea is smoother than it was
*and* now tells the truth about moving.

**Also landed:** the water accumulate reprojects by the wave rather than through
the previous camera alone -- the same defect RT-15 fixed for reflections, and the
sea is the one surface that always moves. Measured **neutral** on both static
cameras (0.915 → 0.924, 0.591 → 0.594, inside the noise); kept because it is the
quantity that actually describes what moved, and it costs one fetch. It wants a
camera dolly on the bridge to be judged properly, which no harness here has.

**New instruments:** `--debug-view=water-motion` and `--debug-view=water-mask`.
Every question about the sea before these existed cost a hand-staged probe.

**What is left, and it is most of the item:** the water's direct light through
`DirectTrace`, and its mirror and refraction rays as signals. **And one piece
this session argues against on evidence:** folding the choose/shade/accumulate
passes into the shared contract. The water accumulate's own header explains why
the contract's geometric validation cannot work for a sea -- a wave lifting the
surface a metre moves the point seen at one pixel by tens of metres at a grazing
angle -- so it validates by the neighbourhood's spread instead, and measuring it
off costs 1.160 → 1.609 of speckle. Folding it in would replace a test that works
with one its own record says fails. **Raise it with the owner before building.**

**Method notes.** `context.Color(...)` and `SetTexture(slot, ...)` were both
proved good by binding the G-buffer normals to the slot and watching them
arrive -- do that before suspecting the plumbing. And the runtime **must** be run
from `build/bin/Release/RageVRuntime`: from the repo root it silently compiles no
shaders and every frame comes back black, which reads as a broken change.

'''
anchor = '### RT-19 — ✅ done 2026-09-08 (uncommitted)'
if s.count(anchor) != 1:
    sys.exit('records anchor matched %d' % s.count(anchor))
s = s.replace(anchor, record + anchor, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('RT-8 recorded')
