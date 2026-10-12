import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import re, sys
R = (_RAGEV + '/docs/')
S = 'C:/Users/ism19/AppData/Local/Temp/claude/C--Users-ism19-Code/69b2a2c1-0da8-459a-8242-c1e5d00ae840/scratchpad/'
old = open(R + 'RT-SERIES-2.md', encoding='utf8').read()
new = open(R + 'RT-ROADMAP.md', encoding='utf8').read()
prev = open(S + 'RT-ROADMAP.before-critic2.md', encoding='utf8').read().replace('\r\n', '\n')

def relabel(t):
    # the same chain applied to the roadmap on 2026-09-25 (second critic)
    for a, b in [(r'RT2-23 \(24([fg])\)', r'RT2-23\1'), (r'RT2-23 24([a-g])\b', r'RT2-23\1'),
                 (r'\b24([a-g])\b', r'23\1'), (r'\b24-ref\b', '23-ref'),
                 (r'\b16([a-e])\b', r'15\1'), (r'\b18([a-d])\b', r'17\1'), (r'\b19([a-e])\b', r'18\1'),
                 (r'\b35([a-d])\b', r'34\1'), (r'\b41([a-d])\b', r'40\1')]:
        t = re.sub(a, b, t)
    t = re.sub(r"RT2-23([a-e])'s", r"RT2-23's step 23\1's", t)
    t = re.sub(r"RT2-23([a-e])\b", r"RT2-23's step 23\1", t)
    return t

def norm(s): return re.sub(r'\s+', ' ', s).strip()
def sec(t, start, end):
    L = t.split('\n')
    s = next(i for i, l in enumerate(L) if l.startswith(start))
    e = next((i for i, l in enumerate(L) if i > s and l.startswith(end)), len(L))
    return L[s:e]

newn = norm(new)
bad = 0
# 1. items
ids = re.findall(r'^### (RT2-\d+[a-z]?) ', old, re.M)
nids = re.findall(r'^### (RT2-\d+[a-z]?) ', new, re.M)
miss = [i for i in ids if i not in nids]
print('items in RT-SERIES-2:', len(ids), '| in roadmap:', len(nids), '| missing:', miss)
bad += len(miss)
# 2. "> " notes of RT-SERIES-2, verbatim after the relabel chain
oq = [l for l in relabel(old).split('\n') if l.startswith('>') and len(norm(l.lstrip('> '))) >= 5]
m = [l for l in oq if norm(l.lstrip('> ')) not in newn]
print('RT-SERIES-2 note lines:', len(oq), '| not verbatim in roadmap:', len(m))
for l in m: print('   NOTE LOST?', l[:140])
bad += len(m)
# 3. section 10 rows of RT-SERIES-2
o10 = [l for l in sec(relabel(old), '## 10.', '## 11') if l.startswith('|') and not re.match(r'^\|[-| :]+\|$', l)]
n10 = [l for l in sec(new, '## 10.', '## 11') if l.startswith('|')]
n10set = set(norm(l) for l in n10)
lastcol = 0; lost = 0
renamed = {'| **RT2-21** · The G-buffer as a designed contract, and one BSDF for every path |  |  |  |  |':
           '| **RT2-21** · The G-buffer as a designed contract |  |  |  |  |'}  # issue 12, intended
for r in o10:
    if norm(r) in n10set: continue
    if r.strip() in renamed and norm(renamed[r.strip()]) in n10set:
        print('   group header renamed on purpose:', renamed[r.strip()][:70]); continue
    cells = [c.strip() for c in r.strip().strip('|').split('|')]
    head = cells[:-1]
    hit = [l for l in n10 if all(c in l for c in head if c)]
    if hit: lastcol += 1
    else:
        lost += 1; print('   ROW LOST?', r[:160])
print('section 10 rows in RT-SERIES-2:', len(o10), '| in roadmap:', len(n10), '| only the "On the roadmap" cell differs:', lastcol, '| lost:', lost)
bad += lost
# 4. the previous roadmap draft's notes and section 10 rows (what this edit changed)
pq = [l for l in relabel(prev).split('\n') if l.startswith('>') and len(norm(l.lstrip('> '))) >= 5]
pm = [l for l in pq if norm(l.lstrip('> ')) not in newn]
print('previous draft note lines:', len(pq), '| changed in this edit:', len(pm))
for l in pm: print('   changed:', l[:150])
p10 = [l for l in sec(relabel(prev), '## 10.', '## 11') if l.startswith('|')]
pc = [l for l in p10 if norm(l) not in n10set]
print('previous draft section 10 rows:', len(p10), '| changed in this edit:', len(pc))
for l in pc: print('   changed:', l[:150])
print('RESULT:', 'OK' if bad == 0 else 'PROBLEMS %d' % bad)
