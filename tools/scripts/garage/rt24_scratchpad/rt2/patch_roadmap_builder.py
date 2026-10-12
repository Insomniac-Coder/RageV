"""Make build_roadmap.py (a copy of build_page.py) read RT-ROADMAP.md: lettered item keys, milestones
by document order, D0-D30, section 11, the 2026-09-25 budget, the new hero numbers."""
import io
import shutil

shutil.copy('build_page.py', 'build_roadmap.py')
p = 'build_roadmap.py'
t = io.open(p, encoding='utf-8').read()


def rep(a, b, count=1):
    global t
    assert t.count(a) == count, (t.count(a), a[:80])
    t = t.replace(a, b)


rep('"""Build the RT-series 2 roadmap page (HTML) from RT-SERIES-2.md.', '"""Build the RT roadmap page (HTML) from RT-ROADMAP.md.')
rep("SRC = sys.argv[1] if len(sys.argv) > 1 else 'RT-SERIES-2.md'", "SRC = sys.argv[1] if len(sys.argv) > 1 else 'RT-ROADMAP.md'")
rep("OUT = sys.argv[2] if len(sys.argv) > 2 else 'rt-series-2.html'", "OUT = sys.argv[2] if len(sys.argv) > 2 else 'rt-roadmap.html'")
rep("""MILESTONES = [
    # id, name, first, last, sessions, midpoint
    ('M0', 'Trustworthy ground', 0, 3, '16-23', 19.5),
    ('M1', 'Correct inputs and early wins', 4, 9, '15-20', 17.5),
    ('M2', 'One light model', 10, 13, '17-22', 19.5),
    ('M3', 'A real frame', 14, 20, '42-52', 47.0),
    ('M4', 'Signals rebuilt', 21, 30, '55-71', 63.0),
    ('M5', 'Glass and water', 31, 33, '19-24', 21.5),
    ('M6', 'Scale', 34, 43, '64-81', 72.5),
    ('M7', 'The fallback and the post chain', 44, 45, '7-10', 8.5),
]


def milestone_of(n):
    for m in MILESTONES:
        if m[2] <= n <= m[3]:
            return m
    return None
""", """MILESTONES = [
    # id, name, first item key (the document order opens it), sessions, midpoint
    ('M0', 'Trustworthy ground', '0', '19-26', 22.5),
    ('M1', 'Correct inputs and early wins', '4', '18-24', 21.0),
    ('M2', 'One light model', '21b', '25-33', 29.0),
    ('M3', 'A real frame', '14', '39-50', 44.5),
    ('M4', 'Signals rebuilt', '21', '81-106', 93.5),
    ('M5', 'Glass and water', '31', '22-29', 25.5),
    ('M6', 'Scale', '34', '64-81', 72.5),
    ('M7', 'The fallback and the post chain', '44', '8-11', 9.5),
]
ITEM_MS = {}      # item key -> milestone tuple, from the document order
MS_ITEMS = {}     # milestone id -> item keys in order
ITEM_KEYS = set()


def milestone_of(key):
    return ITEM_MS.get(key)
""")
rep("""    s = re.sub(r'\\bRT2-(\\d+)([a-e])?\\b',
               lambda m: '<a class="ref" href="#rt2-%s">RT2-%s%s</a>' % (m.group(1), m.group(1), m.group(2) or ''), s)
    s = re.sub(r'\\bD(\\d{1,2})\\b',
               lambda m: ('<a class="dref" href="#d-%s">D%s</a>' % (m.group(1), m.group(1)))
               if int(m.group(1)) <= 11 else m.group(0), s)""", """    def rt2(m):
        key = m.group(1) + (m.group(2) or '')
        anchor = key if key in ITEM_KEYS else m.group(1)
        return '<a class="ref" href="#rt2-%s">RT2-%s</a>' % (anchor, key)
    s = re.sub(r'\\bRT2-(\\d+)([a-g])?\\b', rt2, s)
    s = re.sub(r'\\bD(\\d{1,2})\\b',
               lambda m: ('<a class="dref" href="#d-%s">D%s</a>' % (m.group(1), m.group(1)))
               if int(m.group(1)) <= 30 else m.group(0), s)""")
rep("def render_item(num, title, lines):", "def render_item(key, title, lines):")
rep("    ms = milestone_of(num)", "    ms = milestone_of(key)")
rep("if int(x) <= 11))", "if int(x) <= 30))")
rep("    if num == 0:", "    if key == '0':")
rep("""    out = ['<article class="item" id="rt2-%d">' % num]""", """    out = ['<article class="item" id="rt2-%s">' % key]""")
rep("""    out.append('<div class="item-id">RT2-%d</div>' % num)""", """    out.append('<div class="item-id">RT2-%s</div>' % key)""")
rep("""            '<span class="mrange">RT2-%d to RT2-%d</span><span class="msess">%s sessions</span></div>'
            % (m[0].lower(), m[0], html.escape(m[1]), m[2], m[3], m[4].replace('-', '–')))""",
    """            '<span class="mrange">RT2-%s to RT2-%s</span><span class="msess">%s sessions</span></div>'
            % (m[0].lower(), m[0], html.escape(m[1]), MS_ITEMS[m[0]][0], MS_ITEMS[m[0]][-1], m[3].replace('-', '–')))""")
rep("""GARAGE = [
    ('Reflections', 30.2, 4.0), ('Direct light', 10.3, 3.2), ('Lit pass, becoming the composition', 3.7, 1.0),
    ('Glass layer', 2.4, 0.5), ('TAA (native)', 1.1, 0.8), ('Transparent', 1.1, 0.4),
    ('G-buffer and classification', 0.7, 1.1), ('AO', 0.6, 0.4), ('Bounce light, baked layer', 0.0, 0.3),
    ('Bounce light, realtime layer (only while a Realtime light is near)', 0.0, 1.0),
    ('Ray-world upkeep, light structure', None, 0.5), ('Post, UI and the rest', 1.7, 1.2),
]
HEADLAND = [
    ('The sea', 23.8, 4.0), ('Lit pass, becoming the composition', 6.5, 1.2), ('Reflections', 8.4, 2.0),
    ('Direct light', 3.2, 2.0), ('G-buffer', 2.1, 1.3), ('TAA (native)', 1.0, 0.8),
    ('Bounce light, baked layer', 0.0, 0.3), ('Bounce light, realtime layer', 0.0, 1.0),
    ('AO, fog, glow, post, the rest', 3.6, 1.4), ('Ray-world upkeep, light structure', None, 0.5),
]""", """GARAGE = [
    ('Reflections (with the RT-24 lamp pool and pick)', 36.0, 3.6), ('Direct light', 10.3, 2.9),
    ('Lit pass, becoming the composition', 3.7, 1.0),
    ('Glass layer', 2.4, 0.5), ('TAA (native)', 1.1, 0.8), ('Transparent', 1.1, 0.4),
    ('G-buffer and classification', 0.7, 1.1), ('AO', 0.6, 0.4), ('Bounce light, baked layer', 0.0, 0.3),
    ('Bounce light, live layer (at High: limit 2.0, aim 1.5)', 0.0, 2.0),
    ('Ray-world upkeep, light structure', None, 0.5), ('Post, UI and the rest', 1.7, 1.0),
]
HEADLAND = [
    ('The sea', 23.8, 3.6), ('Lit pass, becoming the composition', 6.5, 1.0), ('Reflections', 8.4, 1.8),
    ('Direct light', 3.2, 1.8), ('G-buffer', 2.1, 1.3), ('TAA (native)', 1.0, 0.8),
    ('Bounce light, baked layer', 0.0, 0.3), ('Bounce light, live layer (at High: limit 2.0, aim 1.5)', 0.0, 2.0),
    ('AO, fog, glow, post, the rest', 3.6, 1.1), ('Ray-world upkeep, light structure', None, 0.5),
]""")
rep("chart('Garage.', GARAGE, 51.8, 14.4)", "chart('Garage.', GARAGE, 59.0, 14.5)")
rep("chart('Bridge, Headland.', HEADLAND, 48.6, 14.5)", "chart('Bridge, Headland.', HEADLAND, 49.0, 14.2)")
rep("""    item_titles = {}
    body_html = []""", """    cur_ms = None
    for line in lines:
        hm = re.match(r'^### RT2-(\\d+[a-z]?) · ', line)
        if hm:
            key = hm.group(1)
            ITEM_KEYS.add(key)
            for ms in MILESTONES:
                if ms[2] == key:
                    cur_ms = ms
            ITEM_MS[key] = cur_ms
            MS_ITEMS.setdefault(cur_ms[0], []).append(key)
    item_titles = {}
    body_html = []""")
rep("""            chunks = re.split(r'\\n(?=### RT2-\\d+ )', '\\n'.join(sl))""",
    """            chunks = re.split(r'\\n(?=### RT2-\\d+[a-z]? · )', '\\n'.join(sl))""")
rep("""                m = re.match(r'^### RT2-(\\d+) · (.*)$', chl[0])
                n = int(m.group(1))""", """                m = re.match(r'^### RT2-(\\d+[a-z]?) · (.*)$', chl[0])
                n = m.group(1)""")
rep("""             9: 'What changed', 10: 'Hard-coded values, placed'}
    for k in range(1, 11):""", """             9: 'What changed', 10: 'Hard-coded values, placed', 11: 'How RageV compares'}
    for k in range(1, 12):""")
rep("""                for n in range(ms[2], ms[3] + 1):
                    short = re.split(r'[;:(]', item_titles.get(n, ''))[0].strip()
                    nav.append('<li><a href="#rt2-%d"><span class="tn">%d</span>%s</a></li>' % (n, n, html.escape(short)))""",
    """                for n in MS_ITEMS[ms[0]]:
                    short = re.split(r'[;:(]', item_titles.get(n, ''))[0].strip()
                    nav.append('<li><a href="#rt2-%s"><span class="tn">%s</span>%s</a></li>' % (n, n, html.escape(short)))""")
rep("    total = sum(m[5] for m in MILESTONES)", "    total = sum(m[4] for m in MILESTONES)")
rep("        w = m[5] / total * 100.0", "        w = m[4] / total * 100.0")
rep("(6.5 / m[5] * 100.0)", "(6.5 / m[4] * 100.0)")
rep("""                     % (m[0].lower(), w, m[0], html.escape(m[1]), m[4], inner, m[0]))""",
    """                     % (m[0].lower(), w, m[0], html.escape(m[1]), m[3], inner, m[0]))""")
rep("""                      % (m[0].lower(), m[0], html.escape(m[1]), m[4].replace('-', '–')))""",
    """                      % (m[0].lower(), m[0], html.escape(m[1]), m[3].replace('-', '–')))""")
rep("<title>RageV RT-Series 2</title>", "<title>RageV RT Roadmap</title>")
rep("<h1>RT-Series 2</h1>", "<h1>RT roadmap</h1>")
rep("""<div class="status">24 September 2026 &middot; your twelve decisions answered &middot; RT-24 in progress, parts of five items built early (not committed) &middot; every hard-coded value the audit found is placed (<a href="#s10">section 10</a>)</div>""",
    """<div class="status">25 September 2026 &middot; replaces RT-Series 2, rebuilt from a comparison with the top engines (<a href="#s11">section 11</a>) &middot; your 31 decisions, D0 to D30, all answered &middot; RT-24 in progress (not committed)</div>""")
rep("""        <li><b>46</b><span>items</span></li>""", """        <li><b>53</b><span>items</span></li>""")
rep("""        <li><b>235&ndash;303</b><span>working sessions</span></li>
        <li class="red"><b>12</b><span>decisions, all answered</span></li>""", """        <li><b>276&ndash;360</b><span>working sessions</span></li>
        <li class="red"><b>31</b><span>decisions, all answered</span></li>""")
rep("""<footer class="page">Red marks your calls: RT-24, your top priority, and your twelve decisions, D0 to D11. The same content is in docs/RT-SERIES-2.md.</footer>""",
    """<footer class="page">Red marks your calls: RT-24, your top priority, and your 31 decisions, D0 to D30. The same content is in docs/RT-ROADMAP.md; the evidence behind section 11 is in docs/RT-ROADMAP-RESEARCH.md.</footer>""")
io.open(p, 'w', encoding='utf-8').write(t)
print('patched')
