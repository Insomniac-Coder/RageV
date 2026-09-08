# -*- coding: utf-8 -*-
"""RT-8 job 1 on the record, and the two defects it uncovered."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'RT-8 job 1' in s:
    sys.exit('already recorded')

RECORD = '''### RT-8 job 1 — 🔨 built and measured 2026-09-09, **off by default**

**What it replaces.** `WaterChooseLamps` and `WaterShadeLamps` are the sea's
private copy of what `DirectTrace` has done for every other surface since
RT-first T5: score the lamps that reach a point, keep K by reservoir sampling,
shade them, trace their shadow rays. The one thing that is genuinely the sea's
is the **lobe** — anisotropic Beckmann about the wind rather than GGX, because
Cox and Munk fitted the sea's slope distribution twice, along the wind and
across it, and that is what turns a highlight from a disc into the streak a low
light lays on water. That is a branch inside `DirectTerm`, not a pass.

So `direct_trace.rvshader` gains an `RV_DIRECT_WATER` variant: the same pass,
compiled again, reading the sea's layer in the four slots the G-buffer uses and
taking the sea's lobe. Everything the two shared is shared in fact now.

**Three defects came out of building it, and two of them were mine from earlier
today.**

1. **The sea's normal is not octahedral.** `FetchWaterPoint` stores the two
   horizontal components and recovers the vertical, because a sea's normal
   always points up — nothing lost, nothing quantised, and a *different*
   encoding from the rest of the frame. Every place this session pointed the
   shared machinery at the sea's surface lane had been running `OctDecode` over
   it since job 3. It shows exactly as a wrong normal should: mean 16.76 →
   14.41 and the 99th percentile 112 → 75 at the glitter camera. `Probe.w` is a
   bitfield now — bit 0 the position lane, bit 1 the up-normal lane — so a
   layer describes itself in one number, and `SignalGuidance` carries it.
2. **The sun was being shaded twice.** The pair this pass writes replaces the
   sea's lamp pair, which the water draw *adds* to a lit loop that already
   walked the directional lights — `ShadeLamp` has no directional branch and
   never had one. Fixed by leaving directional lights to the draw.
3. **The score has to be the term's shape, or the sampler is worse than
   uniform.** `ScoreDirect` scores with a GGX peak at `alpha = roughness²`. The
   sea's roughness *is* the RMS slope, 0.05 to 0.24, so alpha² lands near a
   millionth and the score calls every lamp but the one dead on the mirror
   direction black. The sampler then picks that one almost always, pays it an
   enormous weight, and a four-sample estimate of a heavy tail reads dark and
   spiky however unbiased it is in the limit. Measured before the fix: the
   sea's contrast down a fifth at the pier. `ScoreLamp` in `water_lamps.glsl`
   already makes this argument in its own comment; the shared pass now does the
   same thing.

**Measured, with all three fixed** (bridge, sea band from the mask view):

| camera | speckle, private → shared | sd (detail) |
|---|---|---|
| glitter | 1.362 → 1.356 | 19.52 → 19.56 |
| pier | 1.279 → **1.174** (−8.2%) | 19.76 → 19.80 |

**A cleaner sea with its contrast intact** — which is the result the item wanted.

**And it ships off, on a measurement rather than a doubt.** `DirectWaterTrace`
costs **2.78 ms against the two private passes' 1.18**, because it casts about
**0.86 M more shadow rays** for the same picture. Fourteen per cent of an
11.5 ms frame for eight per cent of one noise metric is the wrong trade until
that ray count is understood. **That is what is left of job 1**, and it is a
day's work rather than a redesign: find where the extra rays go — the leading
suspect is that the shared pass shades every lamp in a small cluster where the
sea's own choose pass keeps four, and the range and cull tests differ.

**Switch:** `--water-direct=on|off`, **off** by default.

### A fourth, found chasing the sea's red speckles (2026-09-09)

The owner reported speckles in the water, red because the bridge's lamps are.
**They are not new** — 0.142% of sea pixels spiking more than 25 levels above
their neighbours, against 0.142% with every one of the day's switches off — but
chasing them found this: the contract opens its history bound to twelve
neighbourhood spreads on a smooth surface, on the argument that a mirror's rays
hardly scatter so its history needs no bounding. That is true of a reflection
and **false of every diffuse-kind signal**, whose estimate is stochastic
however smooth the surface under it is. The sea shows it because its roughness
of about 0.05 reads here as a mirror. `gloss` is already forced to one for
these signals two lines below, for the same reason and in nearly the same
words; the bound now follows it.

**Measured neutral, and kept for the argument:** pier 0.139% → 0.130% of sea
pixels above 25 levels, glitter 0.509% → 0.514%, **garage bit-identical**. So
it is not where the fireflies come from. They survive a tight bound as well as
a loose one, which rules out the temporal clamp and leaves the estimator's own
tail — the same heavy tail defect 3 above describes, one step further along.

'''

anchor = '### RT-8 job 2 — ✅ done 2026-09-09'
if s.count(anchor) != 1:
    sys.exit('anchor matched %d' % s.count(anchor))
s = s.replace(anchor, RECORD + anchor, 1)

# Job 3's table was measured before the normal decode was fixed. Re-taken.
old = '''| camera | speckle, own → contract | sd (detail) | p99.9 |
|---|---|---|---|
| glitter | 1.395 → 1.418 | 19.41 → 19.52 | 217.7 → 217.7 |
| pier | 1.251 → 1.321 | 20.26 → 20.35 | 200.0 → 197.8 |

**The contract is 1.6% and 5.6% noisier on speckle and slightly higher in
contrast**, and that is stated rather than tuned away.'''
new = '''| camera | speckle, own → contract | sd (detail) |
|---|---|---|
| glitter | 1.340 → 1.363 | 19.46 → 19.53 |
| pier | 1.215 → 1.279 | 19.63 → 19.50 |

**Re-taken 2026-09-09 after the sea's normal decode was fixed** (see job 1's
first defect): the gap is 1.7% and 5.2%, against 1.6% and 5.6% before, so the
wrong normal was **not** what this cost. **The contract is a few per cent
noisier on speckle at the same contrast**, and that is stated rather than
tuned away.'''
if s.count(old) != 1:
    sys.exit('job 3 table matched %d' % s.count(old))
s = s.replace(old, new, 1)

# And job 2's, for the same reason.
old2 = '''| camera | speckle | sd (detail) | p99.9 | mean |
|---|---|---|---|---|
| glitter | 1.404 → **1.354** (−3.6%) | 19.61 → 19.52 | 217.6 → 217.2 | 16.48 → 16.76 |
| pier | 1.315 → **1.285** (−2.3%) | 20.22 → 19.75 | 197.2 → 195.5 | 23.63 → 23.46 |'''
new2 = '''| camera | speckle | sd (detail) | p99.9 | mean |
|---|---|---|---|---|
| glitter | 1.404 → **1.354** (−3.6%) | 19.61 → 19.52 | 217.6 → 217.2 | 16.48 → 16.76 |
| pier | 1.315 → **1.285** (−2.3%) | 20.22 → 19.75 | 197.2 → 195.5 | 23.63 → 23.46 |

Re-taken after the normal decode was fixed: **1.405 → 1.363 and 1.309 → 1.279**,
the same result to within a few thousandths.'''
if s.count(old2) != 1:
    sys.exit('job 2 table matched %d' % s.count(old2))
s = s.replace(old2, new2, 1)

old3 = '| RT-8 | 🔨 **jobs 2 and 3 done 2026-09-09** — the layer, its motion, its averaging and its mirror ray are on the contract; the direct light is open | 1-2 d left | **high** | the water on the G-buffer |'
new3 = '| RT-8 | 🔨 **jobs 2 and 3 done, job 1 built and off 2026-09-09** — the layer, its motion, its averaging and its mirror ray are on the contract; the direct light works and is measurably cleaner but costs 0.86 M rays too many | ~1 d left | **high** | the water on the G-buffer |'
if s.count(old3) != 1:
    sys.exit('status row matched %d' % s.count(old3))
s = s.replace(old3, new3, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('RT-8 job 1 recorded, jobs 2 and 3 re-measured')
