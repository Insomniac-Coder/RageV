"""RT-6's record into docs/RT-SERIES.md and the hand-off."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, '### RT-6 —'):
    print('RT-SERIES.md already has RT-6')
else:
    s = rep(s,
        "| The owner's expectation, and the precondition for a weaker blur everywhere. | medium |",
        "| The owner's expectation, and the precondition for a weaker blur everywhere. | medium — **✅ geometric half done 2026-09-07, record below; the still-feedback half waits on RT-8** |")

    RECORD = """
### RT-6 — ✅ the geometric half done 2026-09-07 (uncommitted, both copies staged)

**What it does.** The temporal resolve can now refuse a history because it is a
*different surface*, not only because it reprojected off screen. `taa_guide.rvshader`
packs the G-buffer's clip depth, octahedral normal and signed object id into one
RGBA32F lane and keeps it (the G-buffer is single-buffered and transient, so a
copy is the only way last frame's identity survives); the resolve compares the
two and, where they disagree, **searches the eight neighbours before giving up** --
SVGF's rule, the one `reflection_accumulate` already runs. The colour box stays:
geometry answers "is this the same surface", the box answers "is the light on it
the same", and neither substitutes for the other. `--taa-geometry=off` is the
reference arm.

**It fires where it should.** Measured by staging a probe that paints refused
pixels: **1.1–1.7% of the garage's pixels per frame under the dolly** against
0.10–0.22% for the off-screen rejection that was the only refusal before, and
**3.5–4.2% on the bridge**. On a probe frame they sit on the car's and poles'
silhouettes, the ceiling beams and the pipes -- exactly where a surface is being
uncovered. About a quarter of them come from the normal test alone, some of that
on the graffiti wall's normal map rather than on geometry; left in, because the
neighbour search recovers the history rather than discarding it.

**The picture, and it is the point.** `build/rt3/taa_car_sidebyside.png` (bridge,
Deck camera, frame 90, the 320x200 window where the two arms disagree most, at
3x): the tower's horizontal members, the lamp standards and their heads, the deck
markings, the suspender ropes and the railing are all **visibly sharper** with the
test on. What TAA was doing was keeping history across surfaces it had no right
to and blurring the result.

**The two metrics in this repository could not see it, and that is worth
recording.** "Per-frame change mid-dolly" and "settle to the arm's own converged
still" both scored the geometry arm slightly *worse* (poles 8.34 against 7.79;
settle 2.63 against 2.36). **Both proxies reward keeping more history -- which is
what a ghost is** -- so neither can separate "less ghosting" from "worse". A 4x
supersampled reference did not help either: the RMS against it is 28 levels,
dominated by the difference between SSAA and TAA rather than by disocclusion.
The side-by-side crop is what settled it. Do not use those two proxies to judge a
history-rejection change again.

**The neighbour search earned its place.** Hard rejection alone -- take this frame
whole -- was roughly a wash: it trades a ghost for aliasing at every silhouette.
With the search the same numbers move back toward the reference arm (poles 8.69 →
8.34, car 12.57 → 11.66, settle on the car 3.77 → 3.57) while the sharpness win
stays. Nine taps, and the history fetch turns point where a neighbour served,
because the texels between belong to the other side of the edge.

**Cost:** the guide pass 0.05 ms, the resolve 0.132 → 0.270 ms; about 0.19 ms in
all, and a full-resolution RGBA32F pair, allocated only under TAA. Garage 11.24 →
11.28 ms, bridge Headland 14.43, camp 5.35 -- all within noise of where RT-3.1
left them. FXAA and the other modes allocate nothing and are untouched.

**A defect of the same kind as RT-3's, caught by the same probe.** The two lanes
were declared, bound in `Dispatch`, read by the shader -- and **never passed into
the call**. The patch added the comment saying where they rode and not the two
arguments. Every arm was bit-identical (max 0.0) and the geometric refusal count
was exactly 0.000%, while a probe that returned "refuse everything" *did* change
the frame, because it never read the textures. **The probe that finds this is a
refusal counter, not a frame diff:** paint the refused pixels and count them.
Zero, against an off-screen count that is non-zero, is the tell.

**The still-feedback half is not done, and is deferred by the owner to RT-8.**
The rule is now per-pixel *and* geometrically validated -- reaching that line
means the surface test passed -- but the *value* stays a project setting, because
the sea reads zero velocity and no lane in the G-buffer says "this is water".
Globalising it would smear the bridge's water at 0.98, which is why the project
forces it to 0 there today. Owner's word, 2026-09-07: "your concern about water
motion vectors is valid but we will deal with that once we get on RT-8."
"""
    s = rep(s,
        "\n## The S series, for the record (owner asked 2026-09-06)\n",
        RECORD + "\n## The S series, for the record (owner asked 2026-09-06)\n")
    write(D, s)
    print('RT-SERIES.md: RT-6 record written')

H = 'docs/HANDOFF.md'
s = read(H)
if not has(s, 'RT-6'):
    s = rep(s,
        "**Next is RT-6 (TAA on the G-buffer), owner-chosen over RT-4 -- start only on the green signal;** note RT-6's velocity-driven still rule is wrong on the sea until RT-8 writes its motion, so it ships with a guard or after RT-8.",
        "**RT-6's geometric half is done** (the resolve refuses a history by depth, normal and id, with a neighbour search; visibly sharper on the bridge -- `build/rt3/taa_car_sidebyside.png`). Its still-feedback half is deferred by the owner to RT-8. **Open from the owner, not yet started: improve the denoiser and accumulation, and make the ground reflection less blurry** -- the reflection's young blur is `YoungRadius = 12` and RT-6 is the precondition for weakening it.")
    write(H, s)
    print('HANDOFF.md updated')
