"""RT2-0 attribution arms (2026-09-25): the owner-speed swing, one suspect switched at a time, headless.
Each run's report goes to build/rt24/att_<tag>.txt; speckle counts at the end."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import os
import subprocess
import sys

G = (_RAGEV + '/tools/scripts/garage')
OUT = (_RAGEV + '/build/rt24')
SWING = ['--swing=45', '--rate=3']
ARMS = [
    ('att_base', []),                                          # as it ships (working tree)
    ('att_ref16', ['--stage=many16']),                         # the 16-ray reference
    ('att_a_nohist', ['--reflection-history=off']),            # (a) reflection history off
    ('att_b_alllights', ['--hit-light-sampling=off']),         # (b) every light at each hit
    ('att_c_nodirspec', ['--stage=nodirectspec']),             # (c) DirectTrace specular left out
    ('att_d_nofilter', ['--no-frame-memory']),                 # (d) the frame filter's memory off
    ('att_e_depthoff', ['--stage=taadepthoff']),               # (e) TAA depth test off
    ('att_e_depth1', ['--stage=taadepth1']),                   # (e) TAA depth test at 1%
    ('att_f_image', ['--debug-view=reflection-image']),        # (f) the image-distance view
    ('plane_on', []),                                          # RT-24: NRD's test, as it now ships
    ('plane_off', ['--taa-plane-test=off']),                   # RT-24: the old 2% test
]
only = sys.argv[1:]
for tag, extra in ARMS:
    if only and tag not in only:
        continue
    p = subprocess.run([sys.executable, os.path.join(G, 'spin_measure.py'), tag] + SWING + extra,
                       cwd=G, capture_output=True, text=True, errors='replace')
    open(os.path.join(OUT, tag + '.txt'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
    print('done', tag, p.returncode, flush=True)
tags = [t for t, _ in ARMS if t != 'att_f_image' and (not only or t in only)]
p = subprocess.run([sys.executable, os.path.join(G, 'speckle_count.py')] + tags, cwd=G,
                   capture_output=True, text=True, errors='replace')
open(os.path.join(OUT, 'att_speckles.txt'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
print(p.stdout + p.stderr)
