# -*- coding: utf-8 -*-
"""The seventeenth hand-off: the day's real state, and the ten things I got wrong."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/HANDOFF.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'the ten things this day got wrong' in s:
    sys.exit('already written')

old_header = s[s.index('**Read this first.**'):s.index('**Superseded header.**')]
new_header = '''**Read this first.** Updated 2026-09-09 (evening): **the entry below headed "what this day
got wrong" is the current hand-off.** RT-6 and RT-8 are closed, RT-5 is part done and RT-16's
instrument was found to be measuring the wrong thing. `docs/RT-SERIES.md` opens with a status
table and is the one list. **Before picking anything up, read the ten-item list in that entry**
-- every one of them cost hours today and eight are repeatable by anyone. Older headers follow.

'''

ENTRY = '''## 2026-09-09 (evening): what this day got wrong, and where RT-5 and RT-16 stand

**State.** Everything below is committed except the RT-5 work in progress, which is behind
switches that default to off and is **verified bit-identical** to the last commit at the
garage and the headland. RT-6 and RT-8 are closed. Twenty-three of thirty-three items done.

### The ten things this day got wrong

Written out because eight of them are repeatable by anyone, and three are already in this
project's own notes -- which is the point.

1. **Render changes judged by scalar metrics instead of diff images.** The project's rule
   already says diff images. Speckle and contrast called RT-8's job 2 "a clear win at one
   camera, a wash at the other"; it had smeared the deck's white lights into the tower's
   red across the whole bridge, and the owner saw it in one glance. **A scalar cannot see
   shape.** Diff at the camera the scene is composed for, every time.
2. **"Bit-identical" claimed on a scoped test and stated as though it were broad.** The null
   test compared against the *previous commit*, which already contained the day's changes.
   Say what a null test was taken against.
3. **Not reading what was being replaced. Twice.** `water_shade` borrows three neighbours'
   lamp choices and re-scores them at the shading pixel -- the whole reason a block-rate
   choice keeps its detail -- and that was found at the end of the day, not the start. And a
   half-resolution reflection pass whose result was being discarded was "fixed" by reading
   it, when the thing discarding it was tracing a *sharper* reflection per quad.
4. **Patches that silently did not apply, then measured.** A patch script exited on a failed
   match and the edits after it never ran, so a graph kept passing one block where two were
   needed and never passed a neighbour count -- which is why borrowing 0, 1 and 3 gave
   byte-identical frames. **One grep after every patch.**
5. **Measured from an incremental build after a header layout change.** The garage's mean
   moved 44.533 → 44.590 across two builds of *identical source*; a clean rebuild put it
   back. Third item in `project_ragev_stale_artefacts`. **After adding a member to a widely
   included header, rebuild clean before believing a picture.**
6. **A burst of 150 frames sorted by filename**, so frame 100 came before frame 60. The
   first reading of RT-16 was of a sequence in the wrong order.
7. **A fix built before the cause was identified.** The RT-5 anti-lag was written for a
   1.84-second lag whose mechanism was never established, and moved it by 7%.
8. **Measured through the tone curve and called it a filter's time constant.** The fade's
   per-step ratio is 0.96 early and 0.36 late -- an average decays at a *constant* rate, so
   that shape is the tone curve's shoulder, not a filter. Screenshots are 8-bit and
   post-tonemap; there is no linear capture path in this engine today.
9. **The wrong lights switched.** `BURST_SWITCH` targets the tubes, and **every light in
   that scene defaults to `HalfBake`** -- so part of their contribution is in the baked
   field, which does not re-solve when the intensity goes to zero. The 1.84 seconds is
   largely **a baked light behaving like a baked light**. The owner's actual complaint was
   the *car's* lamps, which settle in 25 frames (0.41 s) over 10.7k pixels.
10. **Two things done without asking**: a workflow spawned (there is a standing rule to ask
    every time, with the agent count), and a switch turned on that moved 70% of the pier's
    pixels, an hour after the owner had caught a regression.

### RT-5 — part done

Five parts. **Part 4, the evidence-driven anti-lag, is built** in both the reflection
accumulator and the temporal resolve: where every surface test has already agreed the
surface is the same and the pixel did not move, a history whose mean sits more than N of the
pixel's *own* standard deviations from what its neighbours report now is treated as news and
the memory restarts. The moments both filters already keep supply the noise estimate, which
is what R4 lacked when it fired on stills twice.

**It is off (`--anti-lag=0`) and unproven**, for the reason in item 7 above: the number it
was aimed at is not a filter's. Parts 1, 2, 3 and 5 are untouched -- the grazing-angle plane
test, the relaxed bound for a converged history, and the young blur's retirement.

**A real defect found and fixed on the way:** `BlurSignal` pushes the whole constant block
and `reflection_blur.rvshader` declared six of its eight vectors, so every draw after it was
a validation error. Two dead lanes cost nothing; a short layout costs the command buffer.

**Two pre-existing validation defects left standing and worth an item each:** a pass pushing
24 bytes to a layout with no push-constant range at all (10 a frame), and
`Renderer3D.signal.blur.pair`'s descriptor set being rewritten while still bound, which
invalidates the command buffer (180 a frame). Both are in the blur path RT-5 exists to
retire.

### RT-16 — the instrument was wrong

Its record says `BURST_SWITCH` on the tubes measures "frames until the reflection is gone".
It does not: the tubes are half-baked, so it measures a bake. **The item needs re-scoping
against the car's lamps and a region the owner actually points at**, and any measurement of
a temporal filter's time constant needs linear values, which this engine cannot currently
capture.

'''

s = s.replace(old_header, new_header, 1)
anchor = '## 2026-09-09: an argument I lost to my own measurement'
i = s.index(anchor)
s = s[:i] + ENTRY + s[i:]
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('hand-off updated')
