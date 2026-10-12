"""Write docs/RT-SERIES-2-FINDINGS.md: the evidence behind docs/RT-SERIES-2.md.

Everything comes from the audit's saved results (inputs.json) plus the roadmap's
own appendix and issue table (for the item each finding and issue goes to).
"""
import json
import re

d = json.load(open('inputs.json', encoding='utf-8'))
road = open('RT-SERIES-2.md', encoding='utf-8').read()


def rows(section_start, section_end):
    s = road[road.index(section_start):road.index(section_end)]
    out = []
    for line in s.split('\n'):
        if line.startswith('| ') and not line.startswith('|---'):
            out.append([c.strip() for c in line.strip().strip('|').split('|')])
    return out


# finding id -> (items, note) from the appendix
fmap = {}
for r in rows('## 8. Appendix', '## 9. What changed'):
    if len(r) >= 4 and r[0] not in ('Finding',):
        fmap[r[0]] = (r[2], r[3])
# issue id -> (fate, item, why) from section 3
imap = {}
for r in rows('**All issues:**', '## 4. The items'):
    if len(r) >= 5 and r[0] != 'Issue':
        imap[r[0]] = (r[2], r[3], r[4])

V = {'confirmed': 'confirmed', 'partly': 'partly confirmed (the problem text is the skeptic\'s corrected version)',
     'added-by-skeptic': 'added by the skeptic', 'unverified': 'not verified'}

out = []
w = out.append
w('# RT-series 2: the evidence')
w('')
w('*The audit behind `docs/RT-SERIES-2.md`, run on 2026-09-24: eight code areas, each inspected and then checked by a skeptic '
  'who re-opened every cited line; an inventory of every open issue; a fresh benchmark on this laptop; and a survey of current '
  'RT-first renderers. Every finding and issue id the roadmap uses is defined here. File and line references are to the working '
  'tree on 2026-09-24 and will drift as the code changes. Nothing here is a decision: the roadmap and the owner\'s answers in its '
  'section 5 are.*')
w('')
w('## Contents')
w('')
w('1. Findings by code area (164 verified)')
w('2. The finding the skeptics rejected')
w('3. Open issues (45) and the owner\'s standing rules')
w('4. The benchmark (2026-09-24)')
w('5. What current RT-first renderers do (the reference survey)')
w('')

# ------------------------------------------------------------------ 1
w('## 1. Findings by code area')
w('')
w('Each finding gives its verdict, severity and scope, what is wrong, what it causes, whether it was measured, the evidence, the '
  'fix direction, the skeptic\'s note where there is one, and the roadmap item it goes to.')
w('')
for a in d['verified_findings']:
    w('### %s' % a['name'])
    w('')
    w('**State of the area.** %s' % a['state_summary'])
    w('')
    if a.get('keep'):
        w('**What to keep.**')
        for k in a['keep']:
            w('- %s' % k)
        w('')
    w('**How it should look in an RT-first engine** (the inspector\'s view; the roadmap decides). %s' % a['target_design'])
    w('')
    for f in a['findings']:
        items, note = fmap.get(f['id'], ('(not mapped)', ''))
        w('#### %s · %s' % (f['id'], f['title']))
        w('')
        w('- **Verdict:** %s. **Severity:** %s. **Kind:** %s. **Scope:** %s.' % (
            V.get(f['verdict'], f['verdict']), f['severity'], f['category'], f['scope']))
        w('- **Roadmap:** %s%s' % (items, ' (%s)' % note if note else ''))
        w('- **What is wrong.** %s' % f['problem'])
        w('- **What it causes.** %s' % f['impact'])
        if f.get('measured'):
            w('- **Measured?** %s' % f['measured'])
        if f.get('already_recorded'):
            w('- **Already recorded?** %s' % f['already_recorded'])
        w('- **Fix direction.** %s' % f['fix_direction'])
        if f.get('evidence'):
            w('- **Evidence:**')
            for e in f['evidence']:
                w('  - %s' % e)
        if f.get('skeptic_note'):
            w('- **Skeptic\'s note.** %s' % f['skeptic_note'])
        w('')

# ------------------------------------------------------------------ 2
w('## 2. The finding the skeptics rejected')
w('')
for r in d['rejected_findings']:
    w('#### %s · %s' % (r['id'], r['title']))
    w('')
    w('**Verdict:** %s. %s' % (r['verdict'], r['reason']))
    w('')

# ------------------------------------------------------------------ 3
iss = d['open_issues']
w('## 3. Open issues and the owner\'s standing rules')
w('')
w('The inventory was taken from HANDOFF.md, RT-SERIES.md, NEXT.md, RT-FIRST.md, RENDERING-REVAMP.md, ROADMAP.md, BAKING-ROADMAP.md, '
  'TEXEL-EMITTERS.md, ENGINE-NOTES.md and the code\'s own markers, on 2026-09-24. The fate of each is the roadmap\'s section 3.')
w('')
for i in iss['issues']:
    fate = imap.get(i['id'])
    w('#### %s · %s' % (i['id'], i['title']))
    w('')
    w('- **Status on 2026-09-24:** %s. **Area:** %s.' % (i['status'], i['area']))
    if fate:
        w('- **Fate in the roadmap:** %s, %s. %s' % (fate[0], fate[1], fate[2]))
    w('- **What the owner sees.** %s' % i['symptom'])
    w('- **Known cause.** %s' % i['known_cause'])
    if i.get('owner_notes'):
        w('- **The owner\'s decisions about it.** %s' % i['owner_notes'])
    w('- **Source.** %s' % i['source'])
    w('')
w('### The owner\'s standing rules (as collected for the audit)')
w('')
for r in iss['owner_rules']:
    w('- %s (%s)' % (r['rule'], r['source']))
w('')
if iss.get('code_markers'):
    w('### Markers left in the code (TODO, FIXME and similar)')
    w('')
    for m in iss['code_markers']:
        w('- `%s:%d` %s' % (m['file'], m['line'], m['text']))
    w('')

# ------------------------------------------------------------------ 4
b = d['benchmark']
w('## 4. The benchmark (2026-09-24)')
w('')
w('The engine\'s own `--benchmark=300`, three runs per shot, 1600x900, the project\'s settings (rays on), Vulkan, validation off for timing '
  'and checked separately with `--validation=on`. Lower is faster.')
w('')
w('- **Build.** %s' % b['build_note'])
w('- **GPU.** %s' % b['gpu'])
w('- **Other processes.** %s' % b['other_processes'])
w('')
for r in b['runs']:
    w('### %s' % r['shot'])
    w('')
    w('- Scene `%s`, camera `%s`, %s, %d frames x %d runs.' % (r['scene'], r['camera'], r['resolution'], r['frames'], r['runs']))
    w('- Frame: %.2f ms mean (runs: %s). GPU: %.2f ms.' % (r['frame_ms_mean'], r['frame_ms_spread'], r['gpu_ms']))
    w('- Rays per frame: %s' % r['rays_per_frame'])
    w('- Lights per fragment: %s' % r['lights_per_fragment'])
    w('- Errors and validation: %s' % r['errors'])
    w('')
    w('| Pass | CPU ms | GPU ms |')
    w('|---|---|---|')
    for p in r['top_passes']:
        w('| %s | %.3f | %.3f |' % (p['pass'], p['cpu_ms'], p['gpu_ms']))
    w('')
w('### The scale test')
w('')
w(b['scale_test'])
w('')
w('### Observations')
w('')
for o in b['observations']:
    w('- %s' % o)
w('')

# ------------------------------------------------------------------ 5
ref = d['reference']
w('## 5. What current RT-first renderers do (the reference survey)')
w('')
w('A survey of published techniques (papers, GDC, SIGGRAPH and HPG talks, vendor and engine documentation), taken as the target to '
  'compare RageV against. The owner\'s rule stands: nothing is adopted because another engine does it; each technique lists what it '
  'would have to prove in RageV.')
w('')
for p in ref['pillars']:
    w('### %s' % p['pillar'])
    w('')
    w('**The target.** %s' % p['target_design'])
    w('')
    for tq in p['techniques']:
        w('#### %s' % tq['name'])
        w('')
        w('- **What it does.** %s' % tq['what_it_does'])
        w('- **Vulkan on this laptop.** %s' % tq['vulkan_support'])
        w('- **Rough cost.** %s' % tq['cost_ballpark'])
        w('- **What it must prove in RageV.** %s' % tq['prove_in_ragev'])
        if tq.get('sources'):
            w('- **Sources:** %s' % '; '.join(tq['sources']))
        w('')
w('### A frame budget for an RT-first frame on this GPU (the survey\'s estimate)')
w('')
w(ref['frame_budget'])
w('')
w('### Hardware notes')
w('')
w(ref['hardware_notes'])
w('')
w('### Known pitfalls')
w('')
for p in ref['pitfalls']:
    w('- %s' % p)
w('')

text = '\n'.join(out).replace('\r\n', '\n')
open('RT-SERIES-2-FINDINGS.md', 'w', encoding='utf-8', newline='\n').write(text)
print(len(text), 'chars', text.count('\n#### '), 'h4 blocks',
      'unmapped findings:', [f['id'] for a in d['verified_findings'] for f in a['findings'] if f['id'] not in fmap],
      'issues without fate:', [i['id'] for i in iss['issues'] if i['id'] not in imap])
