"""T4 done: the plan's 2c section, its task row, the hand-off header and the
memory said the build was broken; they now record the defect (one stray
brace) and the verification."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)
def cut(s, nl, start, end):
    """Remove from the line starting with `start` up to (not including) the line starting with `end`."""
    a = s.find(start.replace('\n', nl)); assert a >= 0, start[:60]
    b = s.find(end.replace('\n', nl), a); assert b >= 0, end[:60]
    return s[:a] + s[b:]

# ---- RT-FIRST.md
p = 'docs/RT-FIRST.md'; s, nl = load(p)
s = rep(s, nl, "| T4 | The shared reconstruction contract as code: the reflection accumulator and its young-history blur generalised to any signal (scalar or RGB, its own hit distance), with the debug views (history length, refusal reason, reach) built once for all | 2a | large | 🔨 IN PROGRESS, halted mid-work: applied and built, but the accumulate shader fails to compile (`have` undeclared) — see §2c; fix the shader, restage, verify |",
       "| T4 | The shared reconstruction contract as code: the reflection accumulator and its young-history blur generalised to any signal (scalar or RGB, its own hit distance), with the debug views (history length, refusal reason, reach) built once for all | 2a | large | ✅ done 2026-09-06 (resumed after a halt): `SignalParams` + `AccumulateSignal`/`BlurSignal`, the diffuse kind compiled from the same files, `reflection-refusal` view; verified pixel-identical to the accepted state (parked 0.0000, dolly 0.0000, all metrics equal), accumulate 0.27 ms — see §2c for the halt's defect (one stray brace) |")
s = rep(s, nl, "## 2c · T4 — IN PROGRESS, HALTED MID-WORK 2026-09-06 (context full); THE BUILD IN BOTH COPIES IS BROKEN — READ THIS FIRST",
       "## 2c · T4 — DONE 2026-09-06 (halted once mid-work with a broken build; fixed and verified on resume)")
s = cut(s, nl, "**What is on disk (both `build/bin/Release/RageVRuntime`", "**What T4 changed (the contract as code):**")
s = rep(s, nl, "**What T4 changed (the contract as code):**",
"""**The halt and the fix.** The T4 patch wrapped the accumulator's candidate block in `#ifdef RV_SIGNAL_DIFFUSE … #else … #endif` by a span ending at the first two-tab `}`; the span's replacement carried that brace in its `#else` branch *and* the original brace survived after `#endif`, so `main()` closed its history block one line early: `if (have)` fell outside the scope that declares `have` ("`'have' : undeclared identifier`", both kinds), the accumulate pass drew nothing (0.007 ms) and the floor rendered wrong (44 levels off). Found on resume by a brace-depth walk of `main()` (it ended at depth -1); the fix was deleting that one line (`reflection_accumulate.rvshader`, the `\\t\\t}` after the candidate block's `#endif`), restaged in both copies. **Lesson for span patches:** when the replacement text contains the span's end line, the end line must be consumed by the span, and a brace-depth count of the function is the check to run before building.

**Verification (2026-09-06, on resume; both copies staged and identical):** runtime benchmark reports 0 shader failures (both kinds compile); parked stills vs the accepted `r5_still` and vs `t3_still`: per-pixel mean 0.0000, max 1 / 0 over 20 frames; `r5_dolly` vs `t4_dolly`: mean 0.0000, max 0; `parked_stats.py` (1.07 0.89 0.76 0.85 | 1.07 1.37 1.68 2.10), `smear_metric.py` and `edge_shake.py` identical to `r5_dolly`/`r5_still`; `--debug-view=reflection-refusal` renders (the debug shader's modes 5+ read the attachment's alpha over the scale through the ramp: blue = a kept history at 0.5/6, specks = refusals at silhouettes); bench: ReflectionAccumulate 0.272 ms (T3: 0.288), the three blurs 0.135/0.122/0.131 (T3: 0.226/0.202/0.193 — the same shader, GPU spread), frame 11.6 ms. Captures: `build/garage_burst/t4_still_*`, `t4_dolly_*`, `t4_refusal.png`.

**What T4 changed (the contract as code):**""")
s = cut(s, nl, "**Last look before the halt (2026-09-06 night):**", "**Traps met:**")
s = rep(s, nl, "- Verification that was due and is still due once the shader compiles: parked stills pixel-identical to `r5_still` (was 0.0000 at T3), the dolly numbers identical to `r5_dolly` (`parked_stats.py`, `smear_metric.py`), `edge_shake.py` unchanged, `--debug-view=reflection-refusal` rendering a ramp, the bench's blur/accumulate columns as before (0.09 / 0.27 ms).",
       "- The verification above is the one that was due; it passed in full.")
save(p, s)

# ---- HANDOFF.md header
p = 'docs/HANDOFF.md'; s, nl = load(p)
i = s.index('**Read this first.** Updated 2026-09-06, night, HALTED MID-T4')
j = s.index(nl, i)
s = s[:i] + ("**Read this first.** Updated 2026-09-06 (resumed after the halt). The engine is going RT-first: `docs/RT-FIRST.md` is the plan and its §2b table is the ordered list. T1-T4 are done and verified pixel-identical to the accepted state (T4's halt was one stray brace in `reflection_accumulate.rvshader`; §2c records it). **Next is T5 (shadows on the reconstruction contract), on the owner's green signal only** — one task per signal, report after each. Everything is uncommitted; both builds and both staged shader folders are at the T4 state. The ninth entry below is the reflection pipeline's state and numbers; the WR-16 R series in `docs/RENDERING-REVAMP.md` is paused until T11 is done.") + s[j:]
save(p, s)

# ---- memory
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_reflection_smear.md'; s, nl = load(p)
a = s.index('**T4 HALTED MID-WORK (2026-09-06 night, context full):')
b = s.index('has it all.**', a) + len('has it all.**')
s = s[:a] + ("**T4 DONE 2026-09-06 (after a halt): the accumulator and its blur are the shared contract "
             "(`SignalParams`, `AccumulateSignal`/`BlurSignal`, a diffuse kind from the same shader files, "
             "the `reflection-refusal` view); the halt's defect was one stray brace from a span patch "
             "(brace-depth walk of `main()` found it) -- verified pixel-identical to the accepted state. "
             "**T5 (shadows on the contract) is next, only on the owner's green signal.** RT-FIRST.md §2c "
             "has the record.**") + s[b:]
save(p, s)
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
old = '- [Reflection reconstruction (2026-09-06 night, uncommitted)](project_ragev_reflection_smear.md) — '
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:i] + '- [Reflection reconstruction + RT-first T1-T4 (2026-09-06, uncommitted)](project_ragev_reflection_smear.md) — RT-first T1-T4 done and pixel-identical, T5 shadows next on the green signal; ' + s[i + len(old):j] + s[j:]
save(p, s)
print('T4 notes done')
