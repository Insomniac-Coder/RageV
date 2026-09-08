# -*- coding: utf-8 -*-
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/HANDOFF.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if '2026-09-09' in s.split('## ')[1][:200]:
    sys.exit('already written')

old_header = s[s.index('**Read this first.**'):s.index('**Superseded header.**')]
new_header = '''**Read this first.** Updated 2026-09-09: **the sixteenth entry below is the current
hand-off** -- RT-8's three remaining jobs are all built. Jobs 2 and 3 are done and on by
default; job 1 works, is measurably cleaner, and **ships off** because it costs 0.86 M
shadow rays too many. `docs/RT-SERIES.md` opens with a status table and is the one list.
**The thing to pick up first is where job 1's extra rays go** -- a day's work, named in
its record. Older headers follow.

'''

new_entry = '''## 2026-09-09: an argument I lost to my own measurement, and the sea joins the engine

**State.** Everything below is committed and pushed. The garage is bit-identical
throughout; the bridge's sea is measurably different and measured at every step.

### The claim I spent a session defending, and it was wrong

Folding the sea's averaging into the signal contract was argued against on this: the
contract's geometric gate cannot hold a sea, because a wave lifting the surface a metre
slides the point seen at one pixel by tens of metres. **Nobody had measured it.** The
number offered in support measured *the sea with no averaging*, which is a different
claim about a different thing.

Measured: the gate keeps **98.7%** of sea pixels at the pier, **98.6%** at the glitter
camera, **92.9%** from the deck. The plane test refuses 1.1%. The claim was false.

The plane test measures how far the surface moved **along its own normal** between
frames -- centimetres for a wave -- against a tolerance of `0.05 + 0.01 x eye distance`,
metres out on a bay. The tens of metres are the point sliding *across* the screen, and
RT-8's own motion attachment already cancels that. **The argument was true of the sea
before the same session taught it to report its own motion, and I never re-took it.**

**A trap worth the entry on its own.** The first null test said the picture had moved,
and it had not. Staging the committed shader against the patched engine runs a pass
whose resource set is written past its own layout -- six bindings declared, eight bound.
**A shader-only A/B is only a control while the layout is the same on both sides.**

### What was built (RT-SERIES has the full records)

* **Job 3 -- the sea's averaging on the contract.** What actually kept it out was not the
  plane test but the **depth buffer**: under the sea it describes the seabed. The depth
  binding takes a second meaning rather than a second binding (`PositionLane`), and
  `SignalGuidance` carries lane indices so the sea joins with no downsample and no copy.
  **A real defect fell out: a pair's twin could only ever be given a *shorter* memory
  than its first half.** The direct light wants that, so it was never noticed; the sea
  runs the other way and was measured holding four frames where it wanted sixteen.
* **Job 2 -- the sea's mirror ray as a signal.** It had **no temporal average anywhere**.
  Speckle 1.405 → 1.363 at glitter, 1.309 → 1.279 at the pier, for 0.16 ms. **And a pass
  was being thrown away every frame:** the S5 trace ran off the preset's block size while
  the shader was handed the config override, zero unless a run asks -- so the water draw
  cast its own rays and read none of it.
* **Job 1 -- the sea's light through DirectTrace.** Built, cleaner (**pier speckle 1.279
  → 1.174 at the same contrast**), and **off by default**: 2.78 ms against the private
  pair's 1.18, casting 0.86 M more shadow rays for the same picture.

### Three defects job 1 uncovered, two of them mine from the same day

1. **The sea's normal is not octahedral** -- two horizontal components with the vertical
   recovered, because a sea's normal always points up. Every place I pointed the shared
   machinery at the sea's surface lane had been running `OctDecode` over it. Silent, and
   worth a seventh of the sea's brightness. `Probe.w` is a bitfield now.
2. **The sun was shaded twice** -- the water draw already walks directional lights, and
   `ShadeLamp` has no directional branch and never had one.
3. **A score must be the term's shape.** `ScoreDirect`'s GGX peak at `alpha = roughness²`
   is a millionth of the sea's lobe, so it called every lamp but the one dead on the
   mirror direction black; the sampler then paid that one an enormous weight. Cost a
   fifth of the sea's contrast.

### The red speckles in the water

Reported by the owner, red because the bridge's lamps are. **Not new:** 0.142% of sea
pixels spiking more than 25 levels above their neighbours, against 0.142% with every one
of the day's switches off. Chasing them found that the contract opens its history bound
to twelve spreads on a smooth surface -- true of a reflection, **false of every
diffuse-kind signal**, and the sea's 0.05 roughness reads here as a mirror. Fixed,
measured **neutral** (garage bit-identical), and kept for the argument. **It also rules
the temporal clamp out as the source:** the fireflies survive a tight bound as well as a
loose one, so what is left is the estimator's own heavy tail -- defect 3 above, one step
further along. That is the thread to pull.

### Switches added

`--water-contract` (on), `--water-ray-contract` (on), `--water-direct` (**off**),
`--water-lamp-slack`. Counters: the sea's acceptance and both its memories now print
beside the reflections', because one set of lanes summed over five signals describes none
of them.

'''

s = s.replace(old_header, new_header, 1)
anchor = '## 2026-09-08: the motion nothing recorded'
i = s.index(anchor)
s = s[:i] + new_entry + s[i:]
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('hand-off updated')
