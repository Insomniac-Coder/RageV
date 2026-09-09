# -*- coding: utf-8 -*-
"""RT-5's real size, and RT-16's region, into the hand-off and the series."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)

P = r'docs/HANDOFF.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)

old = """**It is off (`--anti-lag=0`) and unproven**, for the reason in item 7 above: the number it
was aimed at is not a filter's. Parts 1, 2, 3 and 5 are untouched -- the grazing-angle plane
test, the relaxed bound for a converged history, and the young blur's retirement."""
new = """**It is off (`--anti-lag=0`) and unproven**, for the reason in item 7 above: the number it
was aimed at is not a filter's.

**And the item is smaller than its row said.** Measured on the garage, 120 frames:

    reflection history:  100.0% of glossy pixels kept one, 51.0 frames deep
    reflection refusals: off screen 0.0%, none there 0.0%, normal 0.0%, plane 0.0%, roughness 0.0%

**The plane test refuses nothing.** Part 2 -- "the ceiling and far pipes are refused every
frame today" -- was written from T5's refusal view, *before* RT-6.3 through RT-6.11 landed,
and is no longer true. Part 1 (reject by id, depth and normal) landed in RT-6 and RT-6.5.

| part | state |
|---|---|
| 1. reject by id / depth / normal | **already done** (RT-6, RT-6.5) |
| 2. the grazing-angle plane test | **already done** -- 0.0% plane refusals, measured |
| 3. relax the bound for a converged history | **open, and the live one** |
| 4. the anti-lag | built, off, unproven |
| 5. retire the young blur | open, wants 3 first |

**Part 3 is where to start.** Every one of those 51 frames of history is dragged back toward
a four-sample estimate by the neighbourhood bound -- the -0.16 levels on the floor the item
names. A history that has converged over dozens of frames does not need protecting from its
own noise; a young one does. The counters above are the instrument: relaxing the bound
should raise the depth and leave the refusals at zero."""
if s.count(old) != 1:
    sys.exit('hand-off RT-5 block matched %d' % s.count(old))
s = s.replace(old, new, 1)

old2 = """against the car's lamps and a region the owner actually points at**, and any measurement of
a temporal filter's time constant needs linear values, which this engine cannot currently
capture."""
new2 = """against the car's lamps and a region the owner actually points at**, and any measurement of
a temporal filter's time constant needs linear values, which this engine cannot currently
capture.

**What the car's lamps actually measured** (`BURST_SWITCH="Headlamp,Tail|1.328"`, anti-lag
off, the build verified bit-identical to the last commit first): **10,700 pixels change and
settle in 25 frames, 0.41 s.** That is not "a few seconds", so the *region* is wrong rather
than the build -- the mask was taken by brightness, which finds the lit floor and not the
car's reflection in it. **Ask the owner to point at the pixels before measuring again.**

**Two pieces of scaffolding this item wants and does not have:** a linear capture, so a
filter's time constant can be read without the tone curve's shoulder in it; and a switch
harness aimed at a light the owner names rather than at the tubes."""
if s.count(old2) != 1:
    sys.exit('hand-off RT-16 block matched %d' % s.count(old2))
s = s.replace(old2, new2, 1)
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('hand-off updated')

P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
old3 = "| RT-5 | open | 3-4 d | **high** | the contract validates by the G-buffer; the blur goes |"
new3 = ("| RT-5 | \U0001f528 **part done 2026-09-09** — parts 1 and 2 measured already landed "
        "(0.0% plane refusals); part 4 built and off; **part 3 is the live one** | 1-2 d | "
        "moderate | the contract validates by the G-buffer; the blur goes |")
if s.count(old3) != 1:
    sys.exit('series row matched %d' % s.count(old3))
s = s.replace(old3, new3, 1)

REC = '''### RT-5 — \U0001f528 part done 2026-09-09, and two of its five parts were already finished

**Measured before building, which is what shrank the item.** The garage, 120 frames:

    reflection history:  100.0% of glossy pixels kept one, 51.0 frames deep
    reflection refusals: off screen 0.0%, none there 0.0%, normal 0.0%, plane 0.0%, roughness 0.0%

**Part 2 is done.** "The ceiling and far pipes are refused every frame today" was written
from T5's refusal view, before RT-6.3..RT-6.11 landed. The plane test now refuses **0.0%**.
**Part 1 is done** — rejection by id, depth and normal came with RT-6 and RT-6.5.

**Part 4, the evidence-driven anti-lag, is built and off.** In both the reflection
accumulator and the temporal resolve: where every surface test has already agreed the
surface is the same and the pixel did not move, a history whose mean sits more than N of the
pixel's *own* standard deviations from what its neighbours report now is news, and the
memory restarts. The noise estimate is the moments both filters already keep — which is
exactly what R4 lacked when it fired on stills twice. `--anti-lag=N`, zero being off, with
`--anti-lag-floor` under the noise estimate.

**It is unproven, and that is the honest state**: it was built against RT-16's 1.84-second
number, and that number turned out to be a half-baked light behaving like one, measured
through a tone curve. It moved it by 7%.

**Part 3 is the live one.** 100% of pixels keep a history 51 frames deep, and every one of
those frames the neighbourhood bound drags it back toward a four-sample estimate — the
-0.16 levels on the floor the item names. A converged history does not need protecting from
its own noise; a young one does. The counters above are the instrument: relaxing the bound
should raise the depth and leave the refusals at zero.

**A real defect fixed on the way.** `BlurSignal` pushes the whole constant block and
`reflection_blur.rvshader` declared six of its eight vectors, so every draw after it was a
validation error — silently, unless a run asks for `--validation=on`. **Two dead lanes cost
nothing; a short layout costs the command buffer.**

**Two pre-existing validation defects left standing**, both in the blur path this item
exists to retire: a pass pushing 24 bytes to a layout with no push-constant range (10 a
frame), and `Renderer3D.signal.blur.pair`'s descriptor set rewritten while still bound,
which invalidates the command buffer (180 a frame). The second is undefined behaviour and
deserves an item of its own.

'''
anchor = '### RT-6 — ✅ the still-feedback half, closed 2026-09-09'
if s.count(anchor) != 1:
    sys.exit('series anchor matched %d' % s.count(anchor))
s = s.replace(anchor, REC + anchor, 1)
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('RT-5 recorded')
