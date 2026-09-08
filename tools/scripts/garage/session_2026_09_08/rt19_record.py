import io, sys

P = r'docs/RT-SERIES.md'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'RT-19 — ✅' in s:
    sys.exit('already recorded')

edits = [
 ('| **RT-19** | open — **new** | 0.5-1 d | low | the refusal reasons, totalled per frame |',
  '| **RT-19** | ✅ **done 2026-09-08** | — | — | the refusal reasons, totalled per frame |'),
 ('**Nineteen of thirty-three items are closed, three more are part-done, eleven are open.',
  '**Twenty of thirty-three items are closed, three more are part-done, ten are open.'),
]
for i, (old, new) in enumerate(edits, 1):
    if s.count(old) != 1:
        sys.exit('anchor %d matched %d times' % (i, s.count(old)))
    s = s.replace(old, new, 1)

record = '''### RT-19 — ✅ done 2026-09-08 (uncommitted)

**What was built.** The ray-counter block widened from 16 lanes to 32
(`RayCounters::Count`; the shaders' stride `RayCounterSlot() * 32u` in
`pbr_fragment.glsl`, `taa_resolve` and `rtao_compute` must agree, and the CPU
buffer size follows `Count` on its own). Fifteen new lanes: the temporal
resolve's six refusal reasons and its summed history length, and the reflection
accumulator's pixels, kept, five refusal reasons and summed history length.
`CountTemporal` now takes the reason and the frame count; the accumulator gained
`CountSignal`, counted on the **specular instance only** -- the same shader also
runs for occlusion, the bounce and the direct light, and one set of lanes summed
over four signals would describe none of them. **No plumbing was needed:** the
counter buffer is already declared at set 0 binding 21 under `RV_RAY_SHADOWS`
(which the accumulate pipelines compile with) and already bound to the pass with
the lamp set. Four lines print beside the ray counters.

**What it says on the garage with the moving panel** (120 frames, 1600x900):

```
temporal confidence: 99.7% of pixels reused their history, 55.5 frames deep on average
temporal refusals: off screen 0.0%, no history 0.0%, sky 0.0%, object id 0.2%, depth 0.0%, normal 0.2%
reflection history: 99.9% of glossy pixels kept one, 50.4 frames deep on average
reflection refusals: off screen 0.0%, none there 0.0%, normal 0.0%, plane 0.0%, roughness 0.0%
```

**And that is the finding.** With a near-mirror crossing the frame, the reflection
accumulator refuses **essentially nothing** and holds **fifty frames** on average.
Whatever the smearing on a moving reflector is, it is not a shortage of
refusals -- the filter is keeping almost every history it is offered. The same
question cost an afternoon of staged probes earlier the same day.

**Read them as whole-frame averages**, which is what this item builds: static
pixels dominate the denominator, so a per-region or per-object split is a
separate piece of work and is not here.

**Verified two ways.** The refusal percentages sum to the complement of the
acceptance rate by construction (0.4% against 0.3%, rounding), which is the
self-check to make if a line ever looks wrong; and with `--aa=none` the temporal
lines report "no temporal resolve ran" while the reflection lines still report
real numbers -- so the two sets are live and independent rather than stuck
constants, which is the test this codebase has learned to run before believing a
counter (RT-3's record).

'''
anchor = '### RT-1 — ✅ done 2026-09-06'
if s.count(anchor) != 1:
    sys.exit('records anchor matched %d times' % s.count(anchor))
s = s.replace(anchor, record + anchor, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('RT-19 recorded')
