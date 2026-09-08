# -*- coding: utf-8 -*-
"""RT-8 job 1: the split, built and measured."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'the shared pass splits' in s:
    sys.exit('already recorded')

RECORD = '''#### The shared pass splits, and what is left has a name (2026-09-09, owner-approved)

**`direct_trace.rvshader` now compiles three ways out of one file** -- choose,
shade, and the fused pass exactly as before -- so there is one copy of the
score, the term, the visibility and the field handling rather than three.
Choose walks the cluster list and keeps K lamps by reservoir sampling; shade
reads that and shades them. The blast radius is smaller than it first looked:
`direct_trace` serves the direct light alone, not four signals.

* **What the choose pass writes** is `total / weight[r]` -- one over the
  probability that lamp had of being chosen -- rather than the weight and the
  total apart, so the shade pass needs one number per lamp and no attachment
  for a per-pixel scalar. Two `R32G32B32A32_UINT` attachments: an index is not
  a thing to interpolate, and a weight through a half float would quantise the
  one number the estimate divides by.
* **Four lamps, not eight**, because the reservoir is two uvec4s and the sea's
  own path is capped at four (`RV_LAMP_RESERVOIRS`) -- so the split is
  like-for-like with what it replaces. Above four the fused pass still runs.
* **A split pass always samples.** The fused path's "few enough, shade them
  all" shortcut cannot be written into four slots; where the cell is small the
  reservoir picks those same lamps with weights that still sum right.

**Measured at a matched choice grid** (`--water-lamp-reuse=on`, so both arms
pick per pixel):

| | pass cost | shadow rays | pier speckle | pier sd |
|---|---|---|---|---|
| the sea's own two passes | 3.04 ms | 2.39 M | 1.279 | 19.50 |
| the shared split | **2.80 ms** | **1.97 M** | **1.236** | 19.65 |

**Cheaper, 18% fewer rays, and slightly cleaner at the same contrast.** The
architecture question is answered: the split is the right shape and it works.

**And at the project's default it still loses, for a reason with a name.** With
the choice on the 2×2 block the split costs 1.24 ms against the private pair's
1.29 -- and the sea's contrast falls to 13.75 against 19.50. The cause is not
the split: forcing the choice back to full resolution restores it to 19.65.
**It is that `water_shade` does something the shared pass does not.** It merges
*neighbouring blocks'* reservoirs, validates each by depth and normal, and
**re-scores every candidate at the shading pixel** before using it. That
spatial reuse is where a block choice gets its per-pixel variety back, and
without it one choice really does serve four pixels.

**That is ReSTIR's spatial pass, and it is already filed as RT-10.** So what is
left of job 1 is not job 1: it is RT-10 reaching the shared pass. Until then
`--water-direct` stays **off**, because at the settings the project actually
ships the private path is still the better picture.

**Verified:** with the defaults the whole build is **bit-identical** to the
commit before it at all three bridge cameras -- the fused path, and with it the
opaque direct light, is untouched.

**A trap, and it cost the contrast twice before it was named:** *two* different
blocks meet in this pass -- how many layer texels sit under one of the pass's,
and how many of the pass's sit under one of the choice's. They are different
numbers (the shade pass reads a full-size layer at full rate and a choice made
on the block grid), and sharing one push-constant lane for both made it read
the layer at block rate as well.

**Switch:** `--water-direct-split=on|off`, on whenever `--water-direct` is.

'''

anchor = '#### Where the extra rays went (2026-09-09, the same day)'
if s.count(anchor) != 1:
    sys.exit('anchor matched %d' % s.count(anchor))
i = s.index(anchor)
j = s.index('### A fourth, found chasing the sea')
s = s[:j] + RECORD + s[j:]

old = '| RT-8 | 🔨 **jobs 2 and 3 done, job 1 built and off 2026-09-09** — the layer, its motion, its averaging and its mirror ray are on the contract; the direct light works and is measurably cleaner but costs 0.86 M rays too many | ~1 d left | **high** | the water on the G-buffer |'
new = '| RT-8 | 🔨 **jobs 2 and 3 done, job 1 built and off 2026-09-09** — the layer, its motion, its averaging and its mirror ray are on the contract; the direct light is split into choose and shade and beats the sea\'s own pair at a matched choice grid, and the rest of it is RT-10 | blocked on RT-10 | **high** | the water on the G-buffer |'
if s.count(old) != 1:
    sys.exit('status row matched %d' % s.count(old))
s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('the split recorded')
