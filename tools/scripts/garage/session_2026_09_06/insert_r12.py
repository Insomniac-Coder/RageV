"""Insert the R12 brief ahead of R5 in RENDERING-REVAMP.md, update the
series' order sentence, NEXT.md and the project memory."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
p = 'docs/RENDERING-REVAMP.md'; s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
brief = open('tools/scripts/garage/session_2026_09_06/r12_brief.md', 'rb').read().decode('utf-8').replace('\r\n', '\n').replace('\n', nl)
marker = "#### R5 · At a silhouette, forget fast when the other side is moving"
assert s.count(marker) == 1 and '#### R12' not in s
s = s.replace(marker, brief + marker)
old = "Ten items came out of the engine review at the end of the reflection reconstruction work (HANDOFF ninth entry), plus one rule the owner asked to have written down (R5)."
assert s.count(old) == 1
s = s.replace(old, old + " **R12 was added on the owner's word the same night, after R4's two attempts, and goes next: it is the direct lever on the two things the owner checked and found unchanged, the smear under camera motion and the two-second settle.** Order now: R1 done, R2 done, R3 done, R4 shelved, **R12 next**, then R5, R6, R7, R8, R9, R10, R11.")
open(p, 'wb').write(s.encode('utf-8')); print('R12 inserted; order updated')

p = 'docs/NEXT.md'; s = open(p, 'rb').read().decode('utf-8')
old = "(R2 first; R1 done)"; assert s.count(old) == 1
s = s.replace(old, "(R1-R3 done, R4 shelved; **R12 next**, the short memory under motion with a wide young-history blur, then R5)")
open(p, 'wb').write(s.encode('utf-8')); print('NEXT.md updated')

p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_reflection_smear.md'
s = open(p, encoding='utf-8').read()
old = "One task per green\nsignal: [[feedback-report-each-task-green-signal]]."
assert s.count(old) == 1
s = s.replace(old, "R1-R3 DONE, R4 SHELVED (two attempts: spotting, then no measurable\neffect), **R12 NEXT** (owner-set 2026-09-06 night): cap the memory at\n~8 frames under motion and blur wide while the history is young -- the\ndirect lever on the smear and the two-second settle the owner still\nsees. One task per green signal: [[feedback-report-each-task-green-signal]].")
open(p, 'w', encoding='utf-8').write(s); print('memory updated')
