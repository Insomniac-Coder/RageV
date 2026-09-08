# -*- coding: utf-8 -*-
"""RT-8 job 2 on the record."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'RT-8 job 2' in s:
    sys.exit('already recorded')

RECORD = '''### RT-8 job 2 — ✅ done 2026-09-09: the sea's mirror ray becomes a signal

**What it had.** WR-16 S5 traces the sea's reflection in a pass of its own at a
fraction of the width and height, and the water draw reconstructs it with four
taps weighted by how far each ray went. That is a *spatial* reconstruction of
one frame's rays. There was **no temporal average anywhere in the chain** --
every frame's reflection was that frame's rays and nothing else, which is why
the sea's mirror has always been the noisiest thing on the bridge.

**A defect found while wiring it, and it was free work thrown away every
frame.** The pass ran off the preset's block size and the *shader* was handed
`EngineConfig::WaterReflectionScale`, which is **zero unless a run asks for an
override**. So on a level whose preset traces the sea's reflection separately,
the pass ran, its picture was bound, and the water draw cast its own quad rays
anyway and read none of it. Resolved once at the top of the frame now, and told
to the renderer there -- which it has to be, because the scene block that
carries it to the shader is filled the first time a pass executes.

**What was built.** The traced picture goes through the contract's accumulate
and blur at the trace's own resolution, and the four taps become the joint
bilateral upsample at the end of a reconstruction instead of the whole of one.

* **The specular kind, deliberately.** Every specular test is the right
  question for a sea: has the reflected direction swung (the wave turns), has
  what the ray hits moved (the bridge above it has), and where does the image
  sit now the surface has risen — RT-15's mirror rule, which is what a wave
  does to a reflection every frame. The trace already writes its hit distance
  in alpha with a negative for "no sea", which is the contract's own sentinel.
* **A guidance at the trace's grid**, selected from the sea's layer and never
  averaged: the average of two positions across a wave crest is a point in
  neither. `gbuffer_guide`'s first lane is a whole texel now, so one shader
  serves both a clip depth and the sea's position-and-mask.
* **`SignalParams::NoObjectId`.** The contract weighs a history by whether it
  came from the same object, which needs a lane saying which -- the G-buffer
  has one, the sea's layer does not, because the sea is one surface. Without
  the flag the test falls back to comparing this frame's packed normal against
  last frame's, which on a turning wave differs every frame and would shorten
  the memory for a reason that is not there. Told in `PreviousEye.w` as a third
  value (0 no eye, 1 an eye, 2 an eye on a layer with no ids), so the direction
  test's `> 0.5` is untouched -- a sea wants that test more than most surfaces.
* **The ray distance moved.** The contract takes the picture's alpha for its
  frame count, so the distance the four taps weigh by now comes from the
  accumulate's surface attachment, handed to the draw beside the picture. One
  bit of `WorldGridScale.w` says which.

**Measured** (bridge, sea band from `--debug-view=water-mask`):

| camera | speckle | sd (detail) | p99.9 | mean |
|---|---|---|---|---|
| glitter | 1.404 → **1.354** (−3.6%) | 19.61 → 19.52 | 217.6 → 217.2 | 16.48 → 16.76 |
| pier | 1.315 → **1.285** (−2.3%) | 20.22 → 19.75 | 197.2 → 195.5 | 23.63 → 23.46 |

**Read honestly: a clear win at the glitter camera and about a wash at the
pier.** Glitter loses 3.6% of its speckle for 0.5% of its contrast; the pier
loses 2.3% of each, which is noise and detail going out together. The mean
barely moves, so neither is a haze. The case this is actually for -- a moving
camera, where a signal with no history at all has nothing to fall back on --
has no harness here.

**Cost:** the five new passes total **0.16 ms** at the pier (accumulate 0.100,
guidance 0.035, three blurs 0.027), against a frame of 11.7.

**Switch:** `--water-ray-contract=on|off`, on by default.

'''

anchor = '### RT-8 job 3 — ✅ done 2026-09-09'
if s.count(anchor) != 1:
    sys.exit('anchor matched %d' % s.count(anchor))
s = s.replace(anchor, RECORD + anchor, 1)

old = '| RT-8 | 🔨 **jobs 3 done 2026-09-09** — the layer, its motion and its averaging are on the contract; the light and the rays open | 2-3 d left | **high** | the water on the G-buffer |'
new = '| RT-8 | 🔨 **jobs 2 and 3 done 2026-09-09** — the layer, its motion, its averaging and its mirror ray are on the contract; the direct light is open | 1-2 d left | **high** | the water on the G-buffer |'
if s.count(old) != 1:
    sys.exit('status row matched %d' % s.count(old))
s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('RT-8 job 2 recorded')
