# -*- coding: utf-8 -*-
"""RT-12's record."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

D = 'docs/RT-SERIES.md'
s = read(D)
if has(s, '### RT-12 —'):
    print('already recorded')
    raise SystemExit(0)

s = rep(s,
    "| RT-12 | open | 0.5-1 d | low | the signal debug views, complete |",
    "| RT-12 | ✅ **done 2026-09-07** | — | — | the signal debug views, complete |")

record = """### RT-12 — ✅ done 2026-09-07 (owner-directed: *"finish the whole task, coming back to it again and again doesn't seem like a good approach"*)

**Twenty-five views, all verified to render what their name says.** Eleven are
new, three were **wrong**, and the frame itself is bit-identical (mean 0.000,
max 0.0, on the garage at frame 50 with both signals on).

**The defect, and it is the reason this item earned its place.** `debug_view.rvshader`
chose how to display a value with `if (mode == 8 || mode == 10 || mode == 12)`,
commented *"reflection-picture, direct-light, ao"*. The modes at those numbers
are reflection-picture, **direct-refusal** and **gi-light**. Found by capturing
every view at `--debug-view-mix=1.0`, where a ramp view collapses to the plain
frame and a picture view does not:

| view | showed | should have shown |
|---|---|---|
| `direct-light` | the frame **count** over 64 -- near black | the accumulated picture |
| `ao` | the frame count over 1 -- **near white** | the occlusion picture |
| `direct-refusal` | raw radiance over 6 | its refusal ramp |

**One of them had already cost something.** RT-2's record files *"the AO debug
view reads near-white on a linear ramp"* as evidence for RT-12's log ramp. It
was never the ramp. The view was reading the wrong channel, and a wrong
instrument produced a wrong open item that sat in the list for a day.

**So the fix is not the off-by-one.** Renumbering would leave the next person to
make the same mistake, and this item adds eleven views. The shader is now *told*
which channel and which display to use instead of deriving them from an ordinal,
and the four facts about a view -- source, attachment, channel, display -- are
one switch case each in the frame graph. A view can now only be wrong if its own
row is wrong.

**The eleven new ones cost no bandwidth.** The reconstruction contract gives all
four signals the same attachments and only the reflection's were ever looked at:

- `taa-refusal` -- **the temporal resolve now says why**. `o_Moments.w` was a 0/1
  validity flag that WR-16 S0 wrote for a consumer that does not exist yet; it
  now carries the clause that refused the reprojected texel, in the reflection
  accumulator's own encoding (0 kept, 1 off screen, 2 no history, 3 sky
  crossing, 4 object id, 5 depth, 6 normal) with a half added where the nine-tap
  search found nothing either. Zero still means "reused", so the future validity
  consumer is intact. On the garage mid-dolly it is green on every silhouette
  (object id), red on the car's and the pipes' edges (normal), yellow on the
  near pole (depth), and it draws the graffiti **decals' own id boundaries** --
  which is the instrument confirming a thing the RT-6 record had guessed at.
- `direct-history`, `gi-history`, `ao-history` -- frames behind each texel. The
  reflection has had this since T4; the other three write the identical lane and
  nobody had ever bound it. **It doubles as the confidence as applied**: the
  memory is exactly what RT-6.3's direction test and RT-6.4's match confidence
  scale, so a shortened bar is those tests doing their work.
- `reflection-sigma`, `direct-sigma`, `gi-sigma`, `ao-sigma` -- the pixel's own
  temporal spread from the two stored moments. §11's "temporal variance", and
  the number every bound in the contract is floored at.
- `ao-refusal` -- the one signal whose refusals were never exposed.
- `reflection-normal`, `reflection-motion` -- the stored reflector normal, and
  the virtual image's motion. **RT-6.1 has written that motion lane since it
  landed and nothing had ever looked at it.**

**`--debug-view-log`**, anchored so zero stays zero and one stays one. RT-12
filed it because the direct light saturates at any linear scale.

**A finding from the views themselves, on their first run.** `gi-history` and
`ao-history` come out **byte-identical**, and so do `gi-refusal` and
`ao-refusal`, while `gi-sigma` and `ao-sigma` differ by 63 levels. That is not
misrouting: both signals default to `Memory = 64` and the contract's refusal
tests are **purely geometric**, so two half-resolution signals sharing one
guidance grid refuse exactly the same pixels. The sigma views differ because
that is the only part that depends on the signal's own values. Worth knowing
before either number is read as independent evidence.

**Not built, and the reason rather than a silent omission.** The specification's
§11 asks for the reflection direction as RGB and its frame-to-frame difference
in world space. Reconstructing it in this pass needs an inverse view-projection
(64 bytes) plus the camera, and the debug block's push constants sit inside the
**128 bytes every Vulkan device guarantees** -- it does not fit without a uniform
buffer or a second pass. What is delivered instead comes from stored data and
answers the same questions: `reflection-normal` is the direction test's input,
and `reflection-motion` is what a swinging reflection actually does on screen.

**Verified:** all twenty-five views render (each differs from the plain frame),
each takes the branch its name implies (the mix=1.0 test, now a repeatable
check), the log ramp moves the picture (ao-history 161.7 -> 186.8 mean), and the
rendered frame is unchanged. `--debug-view=taa-refusal` mid-dolly:
`build/garage_burst/rt12_taa_71.png`.

"""
s = rep(s, "### RT-6.7 — written, and the measurement was invalid.",
        record + "### RT-6.7 — written, and the measurement was invalid.")
write(D, s)
print('RT-12 recorded')
