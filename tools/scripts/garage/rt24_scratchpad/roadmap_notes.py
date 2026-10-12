import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

P = (_RAGEV + '/docs/RT-SERIES-2.md')
t = io.open(P, encoding='utf-8', newline='').read()
eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'


def after_heading(heading, note):
    """Insert a note right under an item's heading line."""
    global t
    i = t.index(heading)
    j = t.index('\n', i) + 1
    t = t[:j] + '\n' + note.replace('\n', eol) + '\n' + t[j:]


after_heading('### RT2-0 · Finish RT-24', """> **Progress, 2026-09-24 (built, not committed; RT-24 commits only when fully fixed).** Done: arm 0
> (instruments: `spin_measure.py` scores, `--pan`, the owner-speed swing, `spin_timings.py`); arm C
> (the camera test fixed, and camera-only extra rays behind `--confidence-rays-camera`, off: 6.6 ms a
> frame and no visible gain); the prime suspect of the ghosting found with a pan control (the
> floor's roughness pattern dragged with the reflection) and fixed by a lobe-agreement check;
> the frame filter follows the surface's motion; the speed-guessed camera cut replaced by reported
> cuts (it fired 6-8 times a minute on the owner's drags); the frame filter's no-history pixels
> rebuilt at the pixel centre; the tube lamps' length energy; the reflection memory set by its
> lookup's own uncertainty; the resolve no longer spreads aimed lamp light. Dropped: H (parked
> flicker doubled), J (its motion gate breaks the owner's no-guessed-values rule; ungated it raised
> parked flicker). Owner, live: ghosting "not noticeable", reflections settle "in almost real time".
> Open: speckle and edge jitter at fast moves, the lights' reflections fatter than on 8 Sept (bisected
> to RT-21's highlight fix; engine fixes only, no scene changes). A hard-coded-values audit of the
> chain (262 values, 53 flagged, 23 surviving an adversarial check) is in `build/rt24/audit_phase*.json`.""")

after_heading('### RT2-10 · One light library', """> **Part done early, in RT-24 (2026-09-24):** the tube highlight's length energy -- the radius was
> renormalised and the length was not, so a 3.07 m tube drew its full peak along its whole length.
> Now the same rule as the radius, in the one dimension the length widens (DirectTrace and the lit
> loop). The garage's tube shine fell to about a third; the owner judged it good. **Still owed:** the
> brute-force-tube comparison this item asks for.""")

after_heading('### RT2-16 · Views own their histories', """> **Part done early, in RT-24 (2026-09-24):** a camera cut is reported, never guessed. The speed and
> 45-degree rules are gone; `FrameDesc::CameraCut` comes from the scene (its camera changed, it was
> loaded, or `Scene::RequestCameraCut` / C# `Graphics.CameraCut()`), and from the editor camera's own
> jumps. The hand-kept list of histories to drop is unchanged and still this item's.""")

after_heading('### RT2-23 · Reflection denoiser v2', """> **Parts done early, in RT-24 (2026-09-24), on today's accumulator:** (24b) a content check under
> any motion -- the history is trusted by how alike the two lobes' pictures are where the reflection
> slid across the surface; (24b) the memory from the lookup's own uncertainty (the image depth's
> spread projected through last frame's camera, against the lobe's blur) in place of the travel-based
> smear cap, slack and shrinking floor; (a consistent resolve) the aimed samples' lamp light kept out
> of the neighbour sharing and added back per texel. The rewrite itself is still this item's.""")

after_heading('### RT2-28 · The final temporal pass', """> **Part done early, in RT-24 (2026-09-24):** the frame filter fetches every pixel's history by the
> surface's own motion; the brightness-chosen reflection motion is gone (the reflection keeps its own
> memory). Measured on the garage's slow orbit: pole ghost 10.5 -> 7.7 at 16 rays, parked flicker
> unchanged. Separately, its no-history pixels (first frame, off screen, another surface) are now the
> current frame rebuilt at the pixel centre instead of the raw jittered sample.""")

io.open(P, 'w', encoding='utf-8', newline='').write(t)
print('ok')
