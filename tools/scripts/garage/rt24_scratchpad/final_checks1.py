"""RT-24 final checks, part 1 (headless, main tree): the slow 16-ray swing with the reflection's memory
on and off (exit gate 2), the budget pass present in a runtime swing and absent parked (gate 6)."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import os
import subprocess
import sys

G = (_RAGEV + '/tools/scripts/garage')
OUT = (_RAGEV + '/build/rt24')
RUNS = [
    ('spin_measure.py', 'fin_slow16_on', ['--swing=15', '--rate=2', '--stage=many16']),
    ('spin_measure.py', 'fin_slow16_off', ['--swing=15', '--rate=2', '--stage=many16', '--reflection-history=off']),
    ('spin_timings.py', 'fin_swing_t', ['--swing=45', '--rate=3']),
    ('spin_timings.py', 'fin_still_t', ['--still']),
]
for script, tag, extra in RUNS:
    p = subprocess.run([sys.executable, os.path.join(G, script), tag] + extra, cwd=G,
                       capture_output=True, text=True, errors='replace')
    open(os.path.join(OUT, tag + '.txt'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
    print('done', tag, p.returncode, flush=True)

# The camp, parked: the budget pass must be absent (nothing moves).
RT = (_RAGEV + '/build/bin/Release/RageVRuntime')
p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'), '--project=C:/Users/ism19/Code/RageV/SampleProject',
                    '--scene=scenes/camp.rage', '--rhi=vulkan', '--render-defaults=off', '--width=1600',
                    '--height=900', '--vsync=off', '--frame-time=0.0166', '--benchmark=240'],
                   cwd=RT, capture_output=True, text=True, errors='replace', timeout=1800)
open(os.path.join(OUT, 'fin_camp_t.txt'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
print('done camp', p.returncode)
