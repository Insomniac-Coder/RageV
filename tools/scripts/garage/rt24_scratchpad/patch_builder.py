"""Teach the roadmap page builder notes (blockquotes), the audit table and section 10."""
import io

P = 'C:/Users/ism19/AppData/Local/Temp/claude/C--Users-ism19-Code/69b2a2c1-0da8-459a-8242-c1e5d00ae840/scratchpad/rt2/build_page.py'
t = io.open(P, encoding='utf-8').read()


def replace(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:80])
    t = t.replace(a, b)


replace("SRC = 'RT-SERIES-2.md'\nOUT = 'rt-series-2.html'",
        "SRC = sys.argv[1] if len(sys.argv) > 1 else 'RT-SERIES-2.md'\n"
        "OUT = sys.argv[2] if len(sys.argv) > 2 else 'rt-series-2.html'")

# notes: a run of '>' lines becomes one note; progress notes are quieter than audit notes
replace("""        if line.lstrip().startswith('|'):
            flush()
            rows = []""", """        if line.startswith('>'):
            flush()
            quote = []
            while i < n and lines[i].startswith('>'):
                q = lines[i][1:]
                quote.append(q[1:] if q.startswith(' ') else q)
                i += 1
            done = re.match(r'\\s*\\*\\*(Progress|Parts? done early)', quote[0]) is not None
            out.append('<div class="note%s">%s</div>' % (' done' if done else '', render_blocks(quote)))
            continue
        if line.lstrip().startswith('|'):
            flush()
            rows = []""")

# the audit table: group rows and a chip for what was found
replace("""FATE_CHIP = re.compile(""", """AUDIT_HEAD = ['Value', 'Where', 'What it does', 'Found', 'On the roadmap']
AUDIT_CHIP = re.compile(r'^(Live|Latent|Fixed|Fine|Open)\\b(.*)$')
FATE_CHIP = re.compile(""")
replace("""    if issues:
        cls += ' issues'""", """    if issues:
        cls += ' issues'
    audit = header == AUDIT_HEAD
    if audit:
        cls += ' audit'""")
replace("""    for r in body:
        rid = ''""", """    for r in body:
        if audit and r[0] and not any(c for c in r[1:]):
            out.append('<tr class="group"><td colspan="%d">%s</td></tr>' % (len(header), inline(r[0])))
            continue
        rid = ''""")
replace("""            elif issues and ci == 0:""", """            elif audit and ci == 3 and AUDIT_CHIP.match(c):
                m = AUDIT_CHIP.match(c)
                word, rest = m.group(1), re.sub(r'^[.,]?\\s*', '', m.group(2))
                kind = ' red' if word == 'Live' else ' done' if word in ('Fixed', 'Fine') else ''
                out.append('<td><span class="fate%s">%s</span> %s</td>' % (kind, word, inline(rest)))
            elif issues and ci == 0:""")

# section 10 in the contents
replace("""             9: 'What changed'}
    for k in range(1, 10):""", """             9: 'What changed', 10: 'Hard-coded values, placed'}
    for k in range(1, 11):""")

replace("""<div class="status">24 September 2026 &middot; your twelve decisions answered &middot; nothing built yet</div>""",
        """<div class="status">24 September 2026 &middot; your twelve decisions answered &middot; RT-24 in progress, parts of five items built early (not committed) &middot; every hard-coded value the audit found is placed (<a href="#s10">section 10</a>)</div>""")

replace("""footer.page{margin-top:64px""", """/* ------------------------------------------------------------ notes and the audit table */
.note{margin:0 0 16px;padding:12px 16px 12px 14px;border-left:3px solid var(--accent);background:var(--accent-soft);
  border-radius:0 6px 6px 0;font-size:14.5px;color:var(--text-2)}
.note.done{border-left-color:var(--hair-2);background:var(--raised)}
.note>:last-child{margin-bottom:0}
.note p,.note li{max-width:80ch}
.note ul{margin:8px 0 0;padding-left:1.15em}
.note li{margin:6px 0}
table.audit td:nth-child(1){min-width:200px}
table.audit td:nth-child(3){min-width:170px}
table.audit td:nth-child(4){min-width:250px}
table.audit td:nth-child(5){min-width:170px}
table.audit tr.group td{background:var(--raised);color:var(--text);font-weight:600;padding-top:14px}
.fate.done{border-style:dashed}
footer.page{margin-top:64px""")

io.open(P, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
