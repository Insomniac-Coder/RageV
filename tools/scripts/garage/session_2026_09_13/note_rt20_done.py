# -*- coding: utf-8 -*-
"""RT-20 recorded: the status line and row, the record, and the hand-off entry. CRLF kept."""
import io, sys

ROOT = 'C:/Users/ism19/Code/RageV/'


def edit(path, subs):
    raw = io.open(ROOT + path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    for old, new in subs:
        n = s.count(old)
        if n != 1:
            sys.exit('%s: matched %d times: %r' % (path, n, old[:80]))
        s = s.replace(old, new)
    io.open(ROOT + path, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
    print('updated', path)


RECORD = '''### RT-20 — ✅ done 2026-09-13 (uncommitted): the edge flicker was RT-6's surface test firing on the jitter

**The cause, by taking each suspect out on its own** (`rt20_edges.py`; the garage parked at
the owner's shot, frames 150-189, per-frame change on edge pixels in levels):

| arm | car | wall | poles | tubes |
|---|---|---|---|---|
| as shipped | 7.97 | 4.73 | 4.21 | 11.86 |
| RT-6's surface test off (`--taa-geometry=off`), jitter on | **1.04** | **0.98** | **1.08** | **0.99** |
| RT-6.8's same-surface box off | 7.88 | 4.64 | 4.09 | 11.81 |
| RT-6.11's Catmull-Rom made bilinear | 7.97 | 4.71 | 4.20 | 11.88 |
| the neighbourhood clip off | 7.87 | 4.46 | 3.86 | 11.62 |
| the jitter off (the floor) | 0.97 | 0.86 | 0.98 | 0.82 |

**The mechanism.** With the camera parked, a pixel on an edge has its jittered sample on one
side of the edge in some frames and on the other side in the rest. The test compares this
frame's sample with last frame's, so it read every flip as a different surface, and the
nine-tap search then replaced the pixel's own history -- the coverage blend the jitter exists
to build -- with a neighbour's, which is pure object or pure background. The blend never
formed. Counted with a probe: **2.3% of the garage's pixels a frame had their history replaced
that way, parked.** It has done so since RT-6 (`d34c905`, 2026-09-07), whose record judged a
single frame -- "visibly sharper" -- and set frame-to-frame measures aside for history
changes. That was right for ghosting and blind to this: a parked edge cannot ghost, and no one
measured one. Part of that sharpness was the unblended edge.

**Two fixes, the owner's call "measure both ways and keep the better", and then a third.**

- **A: a still edge keeps its own history.** The centre refused, a neighbour in this frame's
  identity lane is another surface, nothing in the 3x3 moved: keep the centre. The reflection
  accumulator's silhouette rule, gated on stillness.
- **B: last frame's surface is still here.** The centre refused, look for the surface it held
  last frame among this frame's eight neighbours; found, keep the centre.
- **Kept: A, and what the texel showed last frame was not moving either.** A leaked behind fast
  objects (below). Every path of the resolve now writes whether the pixel's surface moved, in
  the sign of the count `o_Moments.x` already stores, and the rule reads it a frame later. The
  first build of it (C) read motion from the resolve's velocity input -- which is the reflection
  composite's lane where that ran, where a mirror moves by its image, and a flat mirror sliding
  along itself has an image that stands still: most of the chrome cube's face read as not
  moving. The kept version reads the scene's own velocity lane, bound as a new input.

**Parked garage** (per-frame change on edges; the 40-frame mean against the test-off arm's, at
its edges):

| arm | car | wall | poles | tubes | mean vs test off |
|---|---|---|---|---|---|
| as shipped | 7.97 | 4.73 | 4.21 | 11.86 | 2.57 |
| A | 1.19 | 1.00 | 1.15 | 1.08 | 1.15 |
| B | 2.06 | 1.02 | 1.17 | 1.27 | 1.60 |
| **kept** | **1.19** | **1.00** | **1.15** | **1.08** | **1.15** |

B left flicker on the headlamps and bumper, where the car is many small parts and last frame's
surface is often not among this frame's neighbours.

**Behind a moving object.** HEAD's garage plus make_moving_scene.py's chrome cube, camera
parked, measured against `--aa=ssaa --ssaa=2` (no history in the resolve) and binned by how many
frames ago the cube left each pixel -- the cube's footprint is where that truth differs from the
same truth with no cube. The cube at 3 m/s, about 3.9 px a frame, error in levels:

| frames since the cube left | 1-2 | 3-5 | 6-10 | 11-20 | 21-40 |
|---|---|---|---|---|---|
| edges: shipped | 23.94 | 20.34 | 18.83 | 18.84 | 13.49 |
| edges: A | 28.35 | 22.82 | 20.43 | 19.76 | 15.03 |
| edges: B | 25.51 | 22.15 | 20.61 | 20.14 | 15.54 |
| edges: first build (C) | 25.29 | 20.80 | 18.83 | 18.57 | 13.54 |
| **edges: kept** | **23.47** | **19.05** | **17.27** | **17.18** | **11.54** |
| wall: shipped | 16.82 | 12.97 | 15.47 | 16.86 | 8.19 |
| wall: A | 19.88 | 15.27 | 17.15 | 18.12 | 10.69 |
| wall: B | 18.67 | 15.38 | 17.43 | 18.18 | 11.35 |
| wall: first build (C) | 18.01 | 14.06 | 16.42 | 17.68 | 10.19 |
| **wall: kept** | **16.80** | **12.94** | **15.46** | **16.89** | **8.26** |

A's leak: an object that clears an edge by two pixels in one frame leaves that edge's 3x3
still, and A kept the history there -- which was the object. The kept version matches shipped on
the wall and is nearer the truth on the edges at every age. At 0.4 m/s (about 0.5 px a frame)
the kept version equals A on the edges (10.65 against shipped's 17.10 at 6-10 frames) and
shipped on the wall to 0.01; B trailed a faint sparkle there. `build/rt5/rt20/final_fastcube.png`:
A minus shipped has the graffiti's outline in green where the cube passed; the kept version has
only the pipe edges the flicker fix steadies.

**The camera dolly** (0.6 m/s for 1.5 s, then still): moving, the kept version is shipped to the
digit (10.45 levels of change beyond the truth's on its edges) -- nothing is still, so the rule
never runs; stopped, 3.23 -> 0.72. B changed 4.6% of the pixels while moving and was further
from the truth where it did.

**The bridge, Deck camera, parked:** top third 12.36 -> 4.60, middle 14.70 -> 3.12 (test off 4.26
and 2.91; B 5.88 and 8.64). `build/rt5/rt20/final_bridge.png`: the tower, lamp standards, railing
and road markings stop shimmering; a single frame is softer than shipped, which is the edges
blended again.

**Counters** (probes moving each kind of pixel into the empty "sky" refusal lane, parked):
neighbour substitutions 2.3% -> 0.1% of pixels a frame; the rule keeps 2.5%.

**Cost:** the resolve 0.367 +- 0.014 ms -> 0.380 +- 0.008 (A,B,B,A twice, 240 frames a run); the
frame 14.53 +- 0.66 -> 14.62 +- 0.50, inside the noise. The rule runs only where the centre was
refused.

**Verified:** the rebuilt engine with HEAD's resolve renders the parked garage bit-identical to
the old build over 18 frames (binding 10 is written only where a layout declares it). Parked,
the kept version equals the first build bit for bit. Release builds clean. scenetest fails the
same 3 checks on Vulkan and 2 on OpenGL as every run of the day on HEAD's shaders -- the AA
switch's descriptor check on bindings 6 and 7, the layered PBR's 33 samplers, "with bodies in
it" -- and no message names binding 10.

**The change.**
- `taa_resolve.rvshader`: `JitterCrossing()` and `SurfaceMotionTexels()`; `MatchingTexel` keeps
  the centre when `JitterCrossing` says so, before the search; `o_Moments.x` is negative where
  the surface moved, on every path, and the count is read through `abs()`; binding 10,
  `u_SurfaceVelocity`. A kept pixel reads 0 in `taa-refusal`, as any kept history does.
- `PostProcess.h/.cpp`: `Dispatch` gains binding 10, written only where the pipeline's reflection
  declares it (the occlusion accumulator shares the shader, and measurements stage older
  copies); `TemporalResolve` gains `surfaceVelocity` -- null means `velocity` already is it.
- `FrameGraphBuilder.cpp`: the resolve passes the scene's own velocity lane.

**Left standing, found on the way.**
- **A thin rim round each ceiling tube reports motion on a parked camera** -- 0.08% of pixels
  flagged moving at frame 400, the same in the composite's lane and the scene's -- so the rule
  does not run there. Not explained. It is why the kept version differs from A on 0.09% of
  pixels at frame 400; the tubes' flicker is 1.08 for both.
- **An object that starts moving two pixels a frame from a standstill** was not moving last
  frame, so the edges it uncovers on its first frame keep its colour. RT-18's coverage mask is
  the structural answer.
- **Sub-pixel members** flipping out of the sample are not a silhouette in this frame's 3x3:
  the bridge's middle third sits 0.21 above the test-off arm.
- **The chrome cube shows vertical stripes once it passes the car** (owner-spotted), in the
  shipped resolve too. Not investigated.

**Instruments** (`tools/scripts/garage/session_2026_09_13/`): `rt20_edges.py` (the cause),
`rt20_arms.py` (A, B, C and the probes, staged on HEAD's resolve whatever the source holds),
`rt20_measure.py` (parked, the cube at two speeds, the dolly, the truth arms, the trail by age),
`rt20_bridge.py`, `rt20_counters.py`, `rt20_cost.py`, `patch_rt20.py`. `stage_run.py` gained the
moving cube and whole-file variants.

'''

edit('docs/RT-SERIES.md', [
    ('**Twenty-two of thirty-four items are closed, four are part-done (RT-4, RT-5, RT-13, RT-16), eight are open.',
     '**Twenty-three of thirty-four items are closed, four are part-done (RT-4, RT-5, RT-13, RT-16), seven are open.'),
    ('| **RT-20** | open — **new, owner-filed 2026-09-13** | 1-2 d | moderate | edges flicker on a parked camera while the jitter is on |',
     '| **RT-20** | ✅ **done 2026-09-13** (uncommitted) — RT-6\'s surface test fired on the jitter at every edge; a still edge now keeps its own history unless what it showed was moving | — | — | edges flicker on a parked camera while the jitter is on |'),
    ('## Records\n\n', '## Records\n\n' + RECORD),
])

HANDOFF = '''## 2026-09-13 (night): the edge flicker was RT-6 refusing the jitter -- RT-20 fixed, uncommitted

**State.** Uncommitted on top of `0de7d38`: `taa_resolve.rvshader`, `PostProcess.h/.cpp`,
`FrameGraphBuilder.cpp`, `docs/RT-SERIES.md` (the RT-20 record), this entry, and the session's
`rt20_*.py` scripts. Release builds clean and every staged copy of the resolve matches the source.
The owner's editor-resaved `showroom.rage` (+ .meta) is still theirs -- not ours to commit.

**What was found.** RT-6's surface test compares this frame's jittered sample with last frame's.
At every edge on a parked camera the sample lands on the object some frames and the background
the rest, so the test refused the history at every flip and the search swapped in a
neighbour's -- pure object or pure background -- and the anti-aliased edge never formed. Turning
the test off (jitter on) took parked edges to the no-jitter floor; nothing else did.

**What was kept, after measuring three.** A still edge keeps its own history across a refusal
when nothing in its 3x3 moved and what it showed last frame was not moving -- a flag carried in
the sign of the moments' count, read from the scene's own velocity (a new binding: the
resolve's velocity input is the reflection composite's lane, where a sliding mirror stands
still). Parked flicker at the floor (car 7.97 -> 1.19, tubes 11.86 -> 1.08), the bridge's
shimmer gone (14.70 -> 3.12), shipped exactly while anything moves, no trail behind the moving
cube. The record has the tables; the owner has the sheets.

**Traps.**
- **The resolve's `u_Velocity` is not the surfaces' motion** wherever reflections ran: RT-6.1
  gives a pixel made mostly of a reflection its virtual image's motion. Ask "did it move" of
  binding 10.
- **`Dispatch` writes descriptor bindings unconditionally** except binding 10, which checks the
  layout. A write to a binding the layout lacks is a driver-level fault, and a staged copy of an
  older shader is exactly that.
- **A staged variant substitutes into the source as it stands.** After patching the source the
  old arms stop building -- or worse, build against the patch. `rt20_arms.py` stages HEAD's file
  whole first.
- **The SSAA truth is no truth for the cube's own face** (its reflection renders differently);
  use it for what is behind the cube.

**Open, in order.**
1. The owner's word on RT-20, then commit and push.
2. The tube rims that report motion while parked (0.08% of pixels) -- the rule skips them.
3. The chrome cube's vertical stripes after it passes the car, in the shipped resolve too.
4. RT-18: the coverage mask, which also covers an object's first frame of motion.
5. The rest of 2026-09-13's list below: RT-5 parts 4 and 5, the highlight bound, RT-16's
   design call, `water_foam` / `irradiance_fill` rounding, the bridge validation crash.

'''

edit('docs/HANDOFF.md', [
    ('**Read this first.** Updated 2026-09-13: **the entry below headed "the histories were\n'
     'rounding the light away" is the current hand-off.**',
     '**Read this first.** Updated 2026-09-13 (night): **the entry below headed "the edge flicker was\n'
     'RT-6 refusing the jitter" is the current hand-off**, and the one after it ("the histories were\n'
     'rounding the light away") still holds the day\'s earlier state and its open list.\n\n'
     '**Superseded header.** Updated 2026-09-13: **the entry below headed "the histories were\n'
     'rounding the light away" is the current hand-off.**'),
    ('## 2026-09-13: the histories were rounding the light away, and the car\'s lamps do linger\n',
     HANDOFF + '## 2026-09-13: the histories were rounding the light away, and the car\'s lamps do linger\n'),
])
