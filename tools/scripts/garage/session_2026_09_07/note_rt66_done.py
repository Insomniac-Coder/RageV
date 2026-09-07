# -*- coding: utf-8 -*-
"""RT-6.6's record into docs/RT-SERIES.md, and its status row."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, '### RT-6.6 —'):
    print('already recorded')
    raise SystemExit(0)

s = rep(s,
    "| **RT-6.6** | open — **new, filed 2026-09-07** | 0.5 d | low | the moments follow the texel the colour came from |",
    "| **RT-6.6** | ✅ **done 2026-09-07** | — | — | the moments follow the texel the colour came from |")

record = """### RT-6.6 — ✅ done 2026-09-07 (uncommitted)

**The defect, from the second outside review, verified at the line.** RT-6 taught
the resolve to search the eight neighbours for a history belonging to this
surface and to fetch the colour from whichever texel won. It did not move the
*moments* with it: they stayed at `texture(u_Moments, historyUV)`, and
`u_Moments` is bound `Sampling::Point`, so that resolves to the **centre texel
-- the one the search had just rejected as a different surface**. Never a
filtering question; two different texels.

Both of the things the moments exist for were therefore wrong on every pixel the
search recovered: `prevMoments.x` is the frame count that sets `alpha =
max(1/frames, 1 - feedback)`, and `.y`/`.z` set the temporal sigma the box may
not narrow below.

**The fix is one bool.** `neighbourServed` is computed once and used by the
colour read and the moments read both, because the defect was exactly the second
copy of that condition going missing. Three lines of code; the rest is comment.

**It fires where the search fires, and only there.** Mean |d| **0.104 levels**
over a 100-frame dolly, with **1.5-2.0% of pixels off by more than one level** --
against RT-6's independently measured neighbour-recovery rate of **1.1-1.7% of
the garage's pixels a frame**. Those two numbers matching is the liveness proof
this session's three dead-plumbing defects taught us to demand.

| where it lands | concentration (share of the difference over share of the frame) |
|---|---|
| wall | 2.34x |
| ceiling (the tube band) | 2.19x |
| car | 1.61x |
| poles | 1.50x |
| floor | **0.80x** |

The strongest 5% of edges carry 12.2% of the difference, 2.43x their area.
`build/garage_burst/rt66_where.png` is the frame beside the map: it is the tube
fixtures, the ceiling beams, the car's silhouette and the vertical pipes, and
**nothing on the flat floor or the flat wall**.

**Parked is not inert, and that was not expected.** 0.059 levels mean, 0.54% of
pixels off by more than two -- about 60% of the dolly's effect, with a camera
that is not moving. **The jitter is why:** it moves the projection a fraction of
a pixel each frame, so at a silhouette the *coverage* flips between frames, the
guide lane genuinely describes a different surface, and the search fires. The
same mechanism as the suspender rope the shader header describes, seen from the
other side. So this fix reaches parked thin geometry, which is where the moments
floor was designed to matter in the first place.

**And that is where the win shows.** Parked, pixels swinging more than four
levels between consecutive frames -- the flicker that the temporal sigma floor
exists to stop:

| region | before | after | |
|---|---|---|---|
| ceiling | 5.690% | **5.434%** | −4.5% |
| car | 8.179% | **7.921%** | −3.2% |
| wall | 6.789% | 6.748% | −0.6% |
| poles | 3.830% | 3.811% | −0.5% |
| floor | 0.163% | 0.163% | 0.0% |

Mean per-frame change is flat (−1.5% to +1.4%), so this is fewer *hard* swings
rather than a general smoothing -- and the floor, which has no thin geometry,
does not move at all. **The mechanism reads straight through:** the sigma floor
keeps a thin member's history when the jitter misses it, and it was being
computed from the fluctuation of the pixel on the *other side of the edge*.

**Mid-dolly, the two proxies, as hints only.** Change −0.09% to −0.74%; detail
−0.26% to −1.41% on four regions and +0.50% on the floor. RT-6's record already
warns that both reward keeping history, and this fix does keep more of it (the
recovered surface's count is generally higher than the rejected centre's), so
neither number can settle direction. **The parked hard-swing count above is the
one that can**, because parked with a pinned timestep the true picture is
static, so a swing is flicker and nothing else.

**Cost: none found.** Frame mean, A,B,B,A: after 10.70 ms, before 10.54, with
each arm varying 0.13-0.28 ms on its own; the TAA resolve's own pass went the
other way (0.203 ms after against 0.225 before). Two runs an arm rather than the
four-run palindrome, so the honest claim is **no cost above about 0.3 ms**, not
zero -- proportionate for one `texelFetch` replacing one `texture` on under two
per cent of pixels.

**Where the review was right and this record was not.** RT-6's own record
describes the neighbour search and says "the history fetch turns point where a
neighbour served, because the texels between belong to the other side of the
edge" -- the exact argument for moving the moments, applied to the colour and
not to them. The defect was one sentence away from being written down, twice,
and was found by someone reading the shader from outside.

"""
s = rep(s, "### RT-6.2 / RT-6.3 — material-aware temporal work",
        record + "### RT-6.2 / RT-6.3 — material-aware temporal work")

write(D, s)
print('RT-6.6 recorded')
