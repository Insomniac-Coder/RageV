# -*- coding: utf-8 -*-
"""RT-8 job 1: where the extra rays went, and why one pass cannot have both."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'why one pass cannot have both' in s:
    sys.exit('already recorded')

RECORD = '''#### Where the extra rays went (2026-09-09, the same day)

**They were not extra.** The sea's private path is hard-capped at four samples
(`RV_LAMP_RESERVOIRS = 4`); the bridge's preset asks for eight. The comparison
was eight samples against four. Swept at matched K:

| samples | private | shared |
|---|---|---|
| 1 | 0.59 M | 0.60 M |
| 2 | 1.19 M | 1.11 M |
| 4 | 2.38 M | **1.96 M** |
| 8 | 2.38 M (capped) | 3.23 M |

**At four the shared pass casts 18% fewer rays** and still cost 2.25 ms against
the private pair's 1.26. So the cost was never the rays.

**It is the scoring, and the reason is the split.** `WaterChooseLamps` runs at
**half the width and half the height** — one lamp choice per 2×2 block
(`EngineConfig::ChooseBlock`, which returns 2 when the reuse is off, and off is
the measured default) — and `WaterShadeLamps` then shades **every pixel** from
that one choice. The expensive part, walking the cluster list and scoring every
lamp, runs at a quarter rate; the cheap part runs at full rate. **The sea's two
passes are not a redundant copy of DirectTrace. The split is why they are
cheap.**

An ablation confirmed the scoring is where the time is: replacing the sea's
lobe in `ScoreDirect` with irradiance alone took the pass from 2.25 ms to 1.90.

**Both halves were then measured, and neither is a win:**

| arm | pass cost | pier speckle | pier sd (detail) |
|---|---|---|---|
| private choose + shade | 1.29 ms | 1.279 | 19.50 |
| shared, per pixel | 3.27 ms | **1.174** | 19.80 |
| shared, per block | **0.86 ms** | **1.092** | **12.32** |

Per pixel: a cleaner sea at its own contrast, for about two milliseconds.
Per block: cheaper than the pair it replaces *and* the frame drops 12.8 → 11.4
ms — and the sea loses **37% of its contrast**, because shading at block rate is
precisely what destroys a glitter track. (A trap on the way: a pass smaller than
the layer it reads must scale its own `gl_FragCoord` by the block, the guidance
pass's `src = dst * d + d/2` rule. Read as its own texel it sampled a corner of
the frame, the sea came back empty, and the pass measured 0.014 ms and no rays.)

**So job 1 stays off, and what is left is not a day's work.** Having both means
splitting the shared `DirectTrace` into a choose pass and a shade pass, the way
the sea already is — an architecture change to a pass four signals use, and one
to put to the owner before building rather than after.

**Switch:** `--water-direct-block=on|off`, off by default, so the trade can be
re-taken without re-deriving it.

'''

anchor = '### A fourth, found chasing the sea'
if s.count(anchor) != 1:
    sys.exit('anchor matched %d' % s.count(anchor))
s = s.replace(anchor, RECORD + anchor, 1)

old = ('**That is what is left of job 1**, and it is a\n'
       "day's work rather than a redesign: find where the extra rays go — the leading\n"
       'suspect is that the shared pass shades every lamp in a small cluster where the\n'
       "sea's own choose pass keeps four, and the range and cull tests differ.")
new = ('**That is what is left of job 1** — and it was chased the same day. See\n'
       'below: the rays were never extra, and the answer turned out to be an\n'
       'architecture question rather than a bug.')
if s.count(old) != 1:
    sys.exit('job 1 tail matched %d' % s.count(old))
s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('the ray finding recorded')
