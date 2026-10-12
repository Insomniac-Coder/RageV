import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

P = (_RAGEV + '/docs/RT-SERIES-2.md')
t = io.open(P, encoding='utf-8', newline='').read()
eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'


def after_heading(heading, note):
    global t
    i = t.index(heading)
    j = t.index('\n', i) + 1
    # after any progress note already there
    while t[j:j + 1] in ('\n', '\r') or t[j:j + 2] == '> ':
        j = t.index('\n', j) + 1
    t = t[:j] + note.replace('\n', eol) + '\n\n' + t[j:]


HEAD = "> **Added from RT-24's hard-coded-values audit (2026-09-24; evidence `build/rt24/audit_phase2.json`).** "

after_heading('### RT2-2 · Instruments that can settle questions', HEAD + """A
> ground-truth render of the garage's wet floor under the tubes, to settle how sharp the lights'
> reflections should be: they fattened when RT-21 removed the highlight cap (bisected: 39% of the near
> floor washed out on 20 Sept, 45% at 50c678f, 48.5% now), and only a reference can say whether today's
> look is right for the floor's roughness (47% of it at 0.1-0.2) or a remaining defect.""")

after_heading('### RT2-6 · Correctness patches that do change the picture', HEAD + """The motion
> lane's own precision. It is half-float, so its rounding grows with speed, while two tests treat
> anything above a fixed 1/8 texel as real motion (`kMotionFloor` in the accumulator, the change record's
> tolerance): at high speed a still surface reads as moving on its own. General form: the tolerance is the
> lane's representable error at that magnitude (the half-float step of the velocity, in texels), or the
> prediction is rounded to the lane's precision before the difference -- so rounding can never fire it.""")

after_heading('### RT2-21 · The G-buffer as a designed contract', HEAD + """Every surface is
> forced to at least roughness 0.045 (`pbr_fragment.glsl`, "fully smooth aliases badly"), so no puddle or
> mirror can be smoother. General form: no floor on the material; any aliasing guard comes from the
> pixel's own footprint (the variance a pixel actually covers), not a fixed number.""")

after_heading('### RT2-23 · Reflection denoiser v2', HEAD + """Two in the resolve (the
> neighbour sharing). (1) A tap whose light is more than half a lamp's own glow skips the outlier clamp
> entirely (`kEmitterShare` 0.5): the lamp hits a rough floor catches by chance pass whole. General form:
> no threshold on where light came from -- split each tap into its lamp part and the rest and bound only
> the rest, or treat every tap by one rule. (2) The disc is a fixed fifth of the lobe (`kReuseFraction`)
> and each tap's weight is capped (`kMaxWeight`). General form: the disc as large as possible while
> blurring the reflected image by at most one texel beyond the lobe's own blur, and weights bounded by
> construction (pairwise MIS over the disc's lobes) instead of by a cap. Also here: sharing lamp light
> correctly (fix 1 of 2026-09-24 keeps it unshared, which removed the halo but costs grain in motion;
> re-aiming each pixel's lamp sample toward the lamp point it hit is RT2-12's reuse arm).""")

after_heading('### RT2-24 · Direct light: visibility reconstructed apart from shading', HEAD + """The
> lamps' own highlights (the direct light's specular half) keep four frames however fast the view
> turns (`PairMemory` 4, and the diffuse kind's floor of 4): a view-dependent highlight averaged over
> views it no longer matches -- smear on the chrome poles and the wet floor in motion. General form: a
> view-dependent payload keeps history only while the view direction stays inside its own lobe (the
> reflection kind's direction test, floor one), the diffuse half keeping its surface-bound floor.""")

after_heading('### RT2-28 · The final temporal pass', HEAD + """Three fixed values in the
> frame filter. (1) "Same surface" allows a 2% depth difference (`kDepthTolerance`), which on a thin
> chrome pole or a grazing floor passes or fails by on-screen size: edge jitter. General form: predict
> the depth this point had last frame (its world point through last frame's camera) and allow one
> texel's worth of depth change on this surface. (2) "Still" is a fixed 0.003 texel of motion
> (`kStillWithinTexels`): at speed, rounding flips pixels between the still and moving handling. General
> form: the motion lane's own precision, as in RT2-6. (3) The brightness curve used to average
> (`Compress`/`Expand`, 1/(1+Y)) is tied to absolute brightness rather than the exposure the tone curve
> will use. General form: weight on the exposed value, so the knee sits at display white.""")

io.open(P, 'w', encoding='utf-8', newline='').write(t)
print('ok')
