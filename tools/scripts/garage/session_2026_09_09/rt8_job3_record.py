# -*- coding: utf-8 -*-
"""RT-8 job 3 on the record: the claim, the measurement, and what was built."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'job 3' in s:
    sys.exit('already recorded')

RECORD = '''### RT-8 job 3 — ✅ done 2026-09-09, and the argument against it was wrong

**The claim, and it was mine.** Folding the sea's frame averaging into the
signal contract was argued against for most of a session on this: the
contract's geometric gate cannot hold a sea, because a wave lifting the surface
a metre slides the point seen at one pixel by tens of metres at a grazing
angle, so a test asking "is the surface still the distance it was" would refuse
the water constantly. The number offered in support -- switching the sea's
averaging off costs 1.160 → 1.609 of speckle -- measures *the sea with no
averaging*, which is a different thing. **Nobody had measured the gate.**

**The measurement.** The water's own accumulate was made to run the contract's
geometric tests beside its own -- the same tolerances, the same nine-texel
search, the same reasons in the same order -- and act on none of them. Bridge,
120 frames, three cameras:

| camera | the gate keeps | without RT-15's exemption | the plane test refuses |
|---|---|---|---|
| glitter | **98.6%** | 98.6% | 1.1% |
| pier | **98.7%** | 98.7% | 1.1% |
| deck | **92.9%** | 92.9% | 5.1% |

**The claim was false.** The plane test measures how far the surface moved
*along its own normal* between frames -- a couple of centimetres for a wave --
against a tolerance of `0.05 + 0.01 × eye distance`, which is metres out on a
bay. The tens of metres in the argument are the point sliding *across* the
screen, and RT-8's own motion attachment already cancels that in the
reprojection. **The argument was true of the sea before the same session
taught it to report its own motion, and nobody re-took it afterwards.**

Verified before it was believed: shifting last frame's stored plane by 100 m
drives the strict arm to **0.0% kept and ~97% plane refusals**, so the test is
live and reading real history; and the whole measurement is **bit-identical**
at all three cameras against a full rebuild of HEAD, so it decides nothing.

**A trap worth the entry on its own.** The first null test said the picture had
moved, and it had not. Staging the committed shader against the patched engine
runs a pass whose resource set is written past its own layout -- six bindings
declared, eight bound; two attachments written, three declared -- which is not
"the engine as it was". The committed *logic* under the patched *layout* was
bit-identical. **A shader-only A/B is only a control while the layout is the
same on both sides.**

**What was then built.**

* **The contract reads a position lane.** `SurfaceAt` rebuilt P from the depth
  buffer, which assumes the signal sits on a surface that buffer describes --
  and under the sea it describes the seabed. That, not the plane test, is what
  kept the sea out. The depth binding now takes a second meaning rather than a
  second binding (`Probe.w`, `SignalParams::PositionLane`): the layer's own
  position in xyz with a mask in w. It is the more exact of the two, and any
  future layer that knows where it is can take the same route.
* **`SignalGuidance` carries lane indices.** The sea's surface pass already
  writes the three lanes the contract validates against -- normal at 0,
  position at 2, motion at 3 -- so the sea needs no downsample and no copy to
  join. The guidance points straight at them.
* **The twin got a memory instead of a ceiling**, and this was a real defect
  in the contract, not a water problem. `frames2 = min(frames, PairMemory)`
  can only ever make a pair's second half *shorter* than its first. The one
  pair before the sea -- the direct light -- wants exactly that, so it was
  never noticed. The sea runs the other way: what enters the water forgets in
  four frames, the glint off it wants sixteen. **Measured: the sea held four
  frames where it wanted sixteen, and read noisier than the pass it replaced.**
  The shortening chain is now a function of a base memory (`ShortenedMemory`)
  and each half goes through it with its own, sharing every reduction. The
  glint now holds **13.3 frames of its sixteen** at the pier.
* **The counters split by signal.** Acceptance and depth had been counted for
  the specular instance alone since RT-19, because one set of lanes summed
  over four signals describes none of them. The sea is a fifth and has its own.

**Measured, final** (bridge, water band taken from `--debug-view=water-mask`,
so the metric is where the water is):

| camera | speckle, own → contract | sd (detail) | p99.9 |
|---|---|---|---|
| glitter | 1.395 → 1.418 | 19.41 → 19.52 | 217.7 → 217.7 |
| pier | 1.251 → 1.321 | 20.26 → 20.35 | 200.0 → 197.8 |

**The contract is 1.6% and 5.6% noisier on speckle and slightly higher in
contrast**, and that is stated rather than tuned away. It is not the memory:
at equal history depth (14.9 frames against a flat 16) the gap holds, and
sweeping every travel cap from 1 to 16 moves it by 0.6%. The travel caps were
**left at the contract's own values**: they exist to stop an average smearing
under a moving camera, there is no bridge dolly in any harness here, and taking
a gain nobody can see for a defect nobody can test is the wrong trade. The dial
(`--water-lamp-slack`) stays so it can be re-taken the day a dolly exists.

**What the sea gains for that 5%:** one code path instead of two, disocclusion,
the silhouette rule, the object-id and material tests, the young-history blur
machinery, and — the part the private pass never had — a memory that shortens
when the camera moves. The private pass kept its full memory however fast the
view travelled, which is precisely why nobody could say what it did on a dolly.

**Regression:** the pair-memory change touches the direct light, the contract's
only other pair. Garage at its benchmark pose: **max 1 level on 0.000% of
pixels.**

**Switch:** `--water-contract=on|off`, on by default.

'''

anchor = '### RT-8 — 🔨 part done 2026-09-08 (uncommitted)'
if s.count(anchor) != 1:
    sys.exit('anchor matched %d' % s.count(anchor))
s = s.replace(anchor, RECORD + anchor, 1)

old = '| RT-8 | 🔨 **part done 2026-09-08** — the layer and its motion; the light and the rays open | 2-4 d left | **high** | the water on the G-buffer |'
new = '| RT-8 | 🔨 **jobs 3 done 2026-09-09** — the layer, its motion and its averaging are on the contract; the light and the rays open | 2-3 d left | **high** | the water on the G-buffer |'
if s.count(old) != 1:
    sys.exit('status row matched %d' % s.count(old))
s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('RT-8 job 3 recorded')
