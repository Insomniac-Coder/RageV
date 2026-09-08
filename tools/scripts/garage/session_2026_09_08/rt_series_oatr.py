"""File the useful half of the OATR document as RT-17, RT-18 and RT-19, and
close RT-15. Every anchor must match exactly once or nothing is written.
"""
import io, sys

P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'RT-17' in s:
    sys.exit('already filed')

edits = []

# --- the status table -------------------------------------------------------
edits.append((
 '| **RT-15** | open — **new, do early** | 2-3 d | moderate | the reflection accumulator reprojects by object motion |',
 '| **RT-15** | ✅ **done 2026-09-08** — both halves | — | — | the reflection accumulator reprojects by object motion |'))

edits.append((
 '| **RT-16** | open — **new** | 1-2 d | moderate | a reflection takes seconds to leave the floor when its light goes out |',
 '| **RT-16** | open — **new** | 1-2 d | moderate | a reflection takes seconds to leave the floor when its light goes out |\n'
 '| **RT-17** | open — **new** | 2-3 d | moderate | the accumulator tests what the ray *hit*, by identity |\n'
 '| **RT-18** | open — **new** | 1-2 d | low | history cannot outlive the silhouette it belongs to |\n'
 '| **RT-19** | open — **new** | 0.5-1 d | low | the refusal reasons, totalled per frame |'))

edits.append((
 '**Six new items on 2026-09-07**, all from two outside reviews of the codebase',
 '**Three new items on 2026-09-08 (RT-17..RT-19)**, from the owner\'s *Object-Aware '
 'Temporal Rendering* document; the section below the reviews records what that document '
 'proposed that this engine already had, what was taken, and what was rejected and why.\n\n'
 '**Six new items on 2026-09-07**, all from two outside reviews of the codebase'))

# --- the build order --------------------------------------------------------
edits.append((
 '| **RT-15** | **The reflection accumulator reprojects by object motion, not only the camera\'s.**',
 '| **RT-17** | **The accumulator tests what the ray hit, by identity -- not only how far away it was.** '
 'Every test the reflection history has is about the *reflector*: the same object, the same plane, the same '
 'facing, the same roughness, the same metallic, and (RT-6.10) how far the reflected thing stood. Nothing '
 'tests **what** it was. So a polished wall that never moves, a camera that never moves, and a car driving '
 'past in front of it: every test passes at full confidence, the history is kept whole, and the car\'s '
 'reflection smears along the wall. RT-6.10 cannot catch it -- a car crossing at a roughly constant distance '
 'does not change the hit *distance*. The trace writes the hit\'s **instance id and normal** into the payload '
 'it already fills, the accumulator keeps them beside the reflector\'s, and a change **scales the confidence '
 'rather than refusing**: a distant environment changes which triangle a ray lands on every frame without '
 'changing what it looks like, so an equality test there would refuse a history that was perfectly good '
 '(the document\'s §62, and it is right). The hit normal is nearly free once the lane exists and catches the '
 'constant-distance case the distance test misses. **Precondition:** the reflection trace has no payload lane '
 'for either today, and RT-14 measured the persistent histories at 128 B/pixel -- so the lane is sized and '
 'measured before it is written, on RT-14\'s own terms. | owner\'s OATR document §15, §17, §61, §62; the half '
 'of RT-6.10 that was filed and not built | The one axis of a reflection\'s history that has never been '
 'validated, and the only one that sees a moving *reflected* object. | medium |\n'
 '| **RT-18** | **History cannot outlive the silhouette it belongs to.** A moving object leaves no trail today '
 'because the per-pixel tests all fire correctly in the band it has vacated -- the id, the depth and the normal '
 'there describe the wall behind, so the object\'s history is refused. That is a guarantee by argument rather '
 'than by construction, and every gap found this week (the missing motion vector of RT-15, the plane residual '
 'of a moving reflector) was a case where one of those tests silently agreed with a history it should have '
 'refused. A coverage mask for **this frame** multiplied into the history weight makes the vacated band empty '
 'by construction. **Take the mask and not the isolated pass**: the document\'s two-pass form (§4, §26) invents '
 'the double-lighting hazard it then warns about in §47, and this engine composites the reflection above the '
 'resolve already (RT-6.1). | owner\'s OATR document §26-27, §46 | The cheap structural guarantee behind the '
 'per-pixel tests, on the class of defect this week kept producing. | small |\n'
 '| **RT-19** | **The refusal reasons, totalled.** Every temporal pass already writes *why* it refused a history '
 'per pixel -- `g_Refusal` in the accumulator, and RT-12\'s reason enum in the resolve -- and nothing ever adds '
 'them up. So the question "is this smear a history wrongly kept, or a signal too thin to average" is answered '
 'with an afternoon of staged probes, which is exactly what 2026-09-08 spent before finding that the objects '
 'were reporting no motion at all. One line beside the ray counters: acceptance rate, the split by which test '
 'refused (id, depth, normal, material, direction, hit, disocclusion, off screen), and the average history '
 'length. **Not a fix, an instrument** -- and the cheapest item on this list. | owner\'s OATR document §56 | '
 'The numbers exist per pixel and are thrown away every frame. | small |\n'
 '| **RT-15** | **The reflection accumulator reprojects by object motion, not only the camera\'s.**'))

for i, (old, new) in enumerate(edits, 1):
    n = s.count(old)
    if n != 1:
        sys.exit('anchor %d matched %d times' % (i, n))
    s = s.replace(old, new, 1)

# --- the record of the review ----------------------------------------------
anchor = '## Records\n'
review = '''## 2026-09-08: **the OATR document, against what this engine already has**

The owner's *Object-Aware Temporal Rendering* proposal, read against the code. It is a
general framework for temporal reuse, aimed at smearing caused by movement, and the
honest summary is that **most of what it specifies is already built here** -- which is
worth recording so it is not proposed again.

**Already built, point for point:** object identity validation (§8, RT-6.5), depth and
normal validation (§10, §11, RT-6), motion validation (§12), material validation (§13,
RT-6.5's metallic), reflection-direction validation (§16, RT-6.3 -- including the
roughness-scaled cosine it proposes), hit validation by distance (§17, RT-6.10), the
reflection-cone idea that hit tolerance should widen with roughness (§63, already in the
lobe-scaled tolerances), continuous confidences multiplied rather than binary rejects
(§21-22, RT-6.4), the 3x3 neighbourhood search (§42, RT-6), disocclusion rejection (§41),
history reset on a camera cut (§51, RT-6.9), variance from stored moments (§25),
per-effect histories (§31), and the debug views (§53-54, RT-12's 27 of them). Its §39
warning -- that a generic final TAA can undo the work -- is the exact defect this engine
hit and fixed by moving the composite above the resolve (RT-6.1).

**Taken, as RT-17, RT-18 and RT-19** (rows in the build order above): the hit's identity,
the current-frame coverage mask, and the totals.

**Rejected, with reasons:**

- **Per-material temporal policies (§32, §33, §67).** A dial per material, against the
  owner's own standing rule that every quality lever is one render setting. The axes it
  wants -- roughness, metallic -- are already read per pixel from the G-buffer, which is
  the same information without the authoring surface.
- **Primitive id and barycentrics (§36).** The document calls it advanced itself. Nothing
  measured here needs triangle-level identity.
- **The isolated render pass (§4, §26).** Its useful half is the mask, which is RT-18. The
  other half invents the double-lighting problem §47 then warns about.
- **The framework and architecture chapters (§58, §68).** This is already the engine's
  shape: one reconstruction contract, four signals, per-effect histories.

**And the limitation worth keeping in mind:** every validation in that document would have
passed on the defect found the same day. A motion vector of zero is a perfectly valid
motion vector, and the engine was reporting zero for everything moved by a fixed-step
script (RT-15's record below). The document validates history *against* motion; it cannot
tell you the motion itself is a lie. That is what RT-19's totals and a staged constant are
for.

## Records
'''
if s.count(anchor) != 1:
    sys.exit('Records anchor matched %d times' % s.count(anchor))
s = s.replace(anchor, review, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('filed RT-17, RT-18, RT-19; RT-15 closed')
