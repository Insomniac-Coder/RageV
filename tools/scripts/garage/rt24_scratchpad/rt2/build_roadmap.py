"""Build the RT roadmap page (HTML) from RT-ROADMAP.md.

A small, purpose-built Markdown subset converter: headings, paragraphs, nested
lists (ul/ol with continuation paragraphs), tables, **bold**, *italic*, `code`.
Section 4's items are parsed into structured cards.
"""
import html
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'RT-ROADMAP.md'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'rt-roadmap.html'

MILESTONES = [
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


# ---------------------------------------------------------------- inline
def inline(text):
    """Escape, then code spans, bold, italic, and cross-reference links."""
    codes = []

    def stash(m):
        codes.append('<code>%s</code>' % html.escape(m.group(1)))
        return '\x00%d\x00' % (len(codes) - 1)

    s = re.sub(r'`([^`]*)`', stash, text)
    s = html.escape(s, quote=False)
    s = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<em>\1</em>', s)
    def rt2(m):
        key = m.group(1) + (m.group(2) or '')
        anchor = key if key in ITEM_KEYS else m.group(1)
        return '<a class="ref" href="#rt2-%s">RT2-%s</a>' % (anchor, key)
    s = re.sub(r'\bRT2-(\d+)([a-g])?\b', rt2, s)
    s = re.sub(r'\bD(\d{1,2})\b',
               lambda m: ('<a class="dref" href="#d-%s">D%s</a>' % (m.group(1), m.group(1)))
               if int(m.group(1)) <= 30 else m.group(0), s)
    return re.sub(r'\x00(\d+)\x00', lambda m: codes[int(m.group(1))], s)


# ---------------------------------------------------------------- blocks
def indent_of(line):
    return len(line) - len(line.lstrip(' '))


MARK = re.compile(r'^(\s*)(?:(-)|(\d+)\.)\s+(.*)$')


def is_marker(line):
    return MARK.match(line) is not None


def parse_list(lines, i, base):
    m = MARK.match(lines[i])
    kind = 'ol' if m.group(3) else 'ul'
    start = int(m.group(3)) if m.group(3) else 1
    items = []  # each: list of ('text', str, newpara) / ('html', str)
    n = len(lines)
    para_break = False
    while i < n:
        line = lines[i]
        if not line.strip():
            j = i + 1
            while j < n and not lines[j].strip():
                j += 1
            if j >= n:
                i = j
                break
            ind = indent_of(lines[j])
            if ind > base:
                para_break = True
                i = j
                continue
            if ind == base and is_marker(lines[j]):
                mm = MARK.match(lines[j])
                if ('ol' if mm.group(3) else 'ul') == kind:
                    i = j
                    continue
            break
        ind = indent_of(line)
        mm = MARK.match(line)
        if ind == base and mm:
            if ('ol' if mm.group(3) else 'ul') != kind:
                break
            items.append([('text', mm.group(4), False)])
            para_break = False
            i += 1
            continue
        if ind > base and items:
            if mm:
                sub, i = parse_list(lines, i, ind)
                items[-1].append(('html', sub))
                para_break = False
                continue
            items[-1].append(('text', line.strip(), para_break))
            para_break = False
            i += 1
            continue
        break
    tag = kind
    attrs = ' start="%d"' % start if kind == 'ol' and start != 1 else ''
    out = ['<%s%s>' % (tag, attrs)]
    for it in items:
        buf = []
        first = True
        for kind_, val, *rest in it:
            if kind_ == 'text':
                newpara = rest[0] if rest else False
                if first:
                    buf.append(inline(val))
                    first = False
                elif newpara:
                    buf.append('<p>%s</p>' % inline(val))
                else:
                    buf.append(' ' + inline(val))
            else:
                buf.append(val)
                first = False
        out.append('<li>%s</li>' % ''.join(buf))
    out.append('</%s>' % tag)
    return '\n'.join(out), i


def split_row(line):
    line = line.strip()
    if line.startswith('|'):
        line = line[1:]
    if line.endswith('|'):
        line = line[:-1]
    return [c.strip() for c in line.split('|')]


AUDIT_HEAD = ['Value', 'Where', 'What it does', 'Found', 'On the roadmap']
AUDIT_CHIP = re.compile(r'^(Live|Latent|Fixed|Fine|Open)\b(.*)$')
FATE_CHIP = re.compile(r'^(Fixed directly.*|Absorbed|Stays .*|Split|Partly.*|Owner decision.*)$')


def render_table(rows, context=''):
    header = split_row(rows[0])
    body = [split_row(r) for r in rows[2:]]
    cls = 'tbl'
    decisions = header[:2] == ['Id', 'Question']
    issues = header[:3] == ['Issue', 'What it is', 'Fate']
    if decisions:
        cls += ' decisions'
    if issues:
        cls += ' issues'
    audit = 'Found' in header and 'On the roadmap' in header
    found_col = header.index('Found') if audit else -1
    if audit:
        cls += ' audit' if header == AUDIT_HEAD else ' audit audit2'
    out = ['<div class="tablewrap"><table class="%s">' % cls, '<thead><tr>']
    for h in header:
        out.append('<th>%s</th>' % inline(h))
    out.append('</tr></thead><tbody>')
    for r in body:
        if audit and r[0] and not any(c for c in r[1:]):
            out.append('<tr class="group"><td colspan="%d">%s</td></tr>' % (len(header), inline(r[0])))
            continue
        rid = ''
        if decisions:
            rid = ' id="d-%s"' % r[0][1:]
        bold_row = r and r[0].startswith('**') and r[0].endswith('**')
        out.append('<tr%s%s>' % (rid, ' class="total"' if bold_row else ''))
        for ci, c in enumerate(r):
            if decisions and ci == 0:
                out.append('<td><span class="did">%s</span></td>' % html.escape(c))
            elif issues and ci == 2 and FATE_CHIP.match(c):
                red = ' red' if c.startswith('Owner decision') else ''
                out.append('<td><span class="fate%s">%s</span></td>' % (red, inline(c)))
            elif audit and ci == found_col and AUDIT_CHIP.match(c):
                m = AUDIT_CHIP.match(c)
                word, rest = m.group(1), re.sub(r'^[.,]?\s*', '', m.group(2))
                kind = ' red' if word == 'Live' else ' done' if word in ('Fixed', 'Fine') else ''
                out.append('<td><span class="fate%s">%s</span> %s</td>' % (kind, word, inline(rest)))
            elif issues and ci == 0:
                out.append('<td class="mono">%s</td>' % html.escape(c))
            else:
                out.append('<td>%s</td>' % inline(c))
        out.append('</tr>')
    out.append('</tbody></table></div>')
    return '\n'.join(out)


def render_blocks(lines):
    out = []
    i = 0
    n = len(lines)
    para = []

    def flush():
        if para:
            out.append('<p>%s</p>' % inline(' '.join(s.strip() for s in para)))
            para.clear()

    while i < n:
        line = lines[i]
        if not line.strip():
            flush()
            i += 1
            continue
        if line.strip() == '---':
            flush()
            i += 1
            continue
        if line.startswith('>'):
            flush()
            quote = []
            while i < n and lines[i].startswith('>'):
                q = lines[i][1:]
                quote.append(q[1:] if q.startswith(' ') else q)
                i += 1
            done = re.match(r'\s*\*\*(Progress|Parts? done early)', quote[0]) is not None
            out.append('<div class="note%s">%s</div>' % (' done' if done else '', render_blocks(quote)))
            continue
        if line.lstrip().startswith('|'):
            flush()
            rows = []
            while i < n and lines[i].lstrip().startswith('|'):
                rows.append(lines[i])
                i += 1
            out.append(render_table(rows))
            continue
        if is_marker(line):
            flush()
            h, i = parse_list(lines, i, indent_of(line))
            out.append(h)
            continue
        if line.startswith('### '):
            flush()
            title = line[4:].strip()
            out.append('<h3 id="%s">%s</h3>' % (slug(title), inline(title)))
            i += 1
            continue
        para.append(line)
        i += 1
    flush()
    return '\n'.join(out)


def slug(s):
    s = re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')
    return s[:60]


# ---------------------------------------------------------------- items
FIELD = re.compile(r'^- \*\*(.+?)\.\*\*\s*(.*)$')
LABELS = {
    'What is wrong now': "What's wrong now",
    'What is wrong now, and what changes': "What's wrong, and what changes",
    'What changes': 'What changes',
    'Owner sees': "What you'll see",
    'Exit gate': 'Done when',
    'If a symptom survives every arm': 'If a symptom survives',
}


def render_item(key, title, lines):
    # optional lead paragraph(s) before the first field
    lead, fields, cur = [], [], None
    for line in lines:
        m = FIELD.match(line)
        if m:
            cur = [m.group(1), [m.group(2)] if m.group(2) else []]
            fields.append(cur)
        elif cur is None:
            lead.append(line)
        else:
            cur[1].append(line[2:] if line.startswith('  ') else line)
    meta = {}
    body = []
    foot = []
    for label, content in fields:
        text = '\n'.join(content).strip('\n')
        if label == 'Scope':
            full = ' '.join(content)
            sm = re.match(r'(.*?)\s*\*\*Depends on\.\*\*\s*(.*?)\s*\*\*Risk\.\*\*\s*(.*)$', full)
            if sm:
                meta['scope'] = sm.group(1).strip().rstrip('.')
                meta['depends'] = sm.group(2).strip()
                meta['risk'] = sm.group(3).strip()
            continue
        if label == 'Size':
            meta['size'] = text.strip()
            continue
        if label in ('Touches', 'Closes'):
            foot.append((label, text))
            continue
        body.append((LABELS.get(label, label), text))
    ms = milestone_of(key)
    risk = meta.get('risk', '')
    level = 3 if risk.lower().startswith('high') else 2 if risk.lower().startswith('medium') or 'to medium' in risk.lower() else 1
    risk_word = re.match(r'([A-Za-z ]+?)(?:[.:,]|$)', risk)
    risk_word = risk_word.group(1).strip() if risk_word else risk
    size = re.sub(r'\s*sessions?\b.*$', '', meta.get('size', '')).replace('-', '–')
    needs = sorted(set(int(x) for x in re.findall(r'\bD(\d{1,2})\b', meta.get('depends', '') + ' ' + title) if int(x) <= 30))
    tags = []
    if key == '0':
        tags.append('<span class="chip red">Your top priority</span>')
    for d in needs:
        tags.append('<a class="chip red" href="#d-%d">Per D%d</a>' % (d, d))
    out = ['<article class="item" id="rt2-%s">' % key]
    out.append('<header class="item-head">')
    out.append('<div class="item-id">RT2-%s</div>' % key)
    out.append('<h3 class="item-title">%s</h3>' % inline(title))
    out.append('<div class="item-meta">')
    if ms:
        out.append('<span class="chip">%s</span>' % ms[0])
    if meta.get('scope'):
        out.append('<span class="chip">%s</span>' % html.escape(meta['scope']))
    if size:
        out.append('<span class="chip">%s sessions</span>' % html.escape(size))
    if risk:
        out.append('<span class="risk" title="%s"><span class="pips" data-level="%d"><i></i><i></i><i></i></span>%s risk</span>'
                   % (html.escape(risk, quote=True), level, html.escape(risk_word.lower())))
    out.extend(tags)
    out.append('</div></header>')
    out.append('<div class="item-body">')
    if any(l.strip() for l in lead):
        out.append('<div class="item-lead">%s</div>' % render_blocks(lead))
    for label, text in body:
        out.append('<section class="f"><h4>%s</h4><div class="fbody">%s</div></section>'
                   % (html.escape(label), render_blocks(text.split('\n'))))
    if meta.get('depends') or meta.get('risk'):
        out.append('<section class="f small"><h4>Depends on</h4><div class="fbody"><p>%s</p></div></section>'
                   % inline(meta.get('depends', '')))
        out.append('<section class="f small"><h4>Risk</h4><div class="fbody"><p>%s</p></div></section>'
                   % inline(meta.get('risk', '')))
    out.append('</div>')
    if foot:
        out.append('<footer class="item-foot">')
        for label, text in foot:
            out.append('<div><span class="lbl">%s</span> %s</div>' % (label, inline(' '.join(text.split('\n')))))
        out.append('</footer>')
    out.append('</article>')
    return '\n'.join(out)


def milestone_banner(m):
    return ('<div class="mbanner" id="%s"><span class="mid">%s</span><span class="mname">%s</span>'
            '<span class="mrange">RT2-%s to RT2-%s</span><span class="msess">%s sessions</span></div>'
            % (m[0].lower(), m[0], html.escape(m[1]), MS_ITEMS[m[0]][0], MS_ITEMS[m[0]][-1], m[3].replace('-', '–')))


# ---------------------------------------------------------------- charts
GARAGE = [
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
]
SCALE = 64.0


def bar(v, cls):
    if v is None:
        return '<div class="bar %s"><span class="val">not split out</span></div>' % cls
    w = max(v / SCALE * 100.0, 0.0)
    return ('<div class="bar %s"><span class="fill" style="width:%.2f%%"></span><span class="val">%s ms</span></div>'
            % (cls, w, ('%.1f' % v).rstrip('0').rstrip('.') if v else '0'))


def chart(title, rows, total_today, total_target):
    out = ['<figure class="chart"><figcaption><strong>%s</strong> GPU time per frame at native 2560x1600. '
           'Upper bar: today, scaled up from the measured 1600x900 frame (an estimate). Lower bar: the proposed target. '
           'Shorter is faster.</figcaption>' % html.escape(title)]
    out.append('<div class="legend"><span><i class="sw today"></i>Today, at native (estimate)</span><span><i class="sw target"></i>Proposed target</span></div>')
    out.append('<div class="rows">')
    for label, t, g in rows:
        out.append('<div class="crow"><div class="clabel">%s</div><div class="cbars">%s%s</div></div>'
                   % (html.escape(label), bar(t, 'today'), bar(g, 'target')))
    line = 16.7 / SCALE * 100.0
    out.append('<div class="crow total"><div class="clabel">Whole frame</div><div class="cbars">%s%s'
               '<span class="budget" style="left:%.2f%%"><span>60 FPS, 16.7 ms</span></span></div></div>'
               % (bar(total_today, 'today'), bar(total_target, 'target'), line))
    out.append('</div></figure>')
    return '\n'.join(out)


# ---------------------------------------------------------------- page
def main():
    text = open(SRC, encoding='utf-8').read().replace('\r\n', '\n')
    lines = text.split('\n')
    # sections by '## '
    sections = []
    cur = None
    intro = []
    for line in lines[1:]:
        if line.startswith('## '):
            cur = [line[3:].strip(), []]
            sections.append(cur)
        elif cur is None:
            intro.append(line)
        else:
            cur[1].append(line)

    def sec_id(title):
        m = re.match(r'(\d+)\.', title)
        if m:
            return 's%s' % m.group(1)
        return {'The short version': 's-short', 'Words this document uses': 's-words'}.get(title, slug(title))

    cur_ms = None
    for line in lines:
        hm = re.match(r'^### RT2-(\d+[a-z]?) · ', line)
        if hm:
            key = hm.group(1)
            ITEM_KEYS.add(key)
            for ms in MILESTONES:
                if ms[2] == key:
                    cur_ms = ms
            ITEM_MS[key] = cur_ms
            MS_ITEMS.setdefault(cur_ms[0], []).append(key)
    item_titles = {}
    body_html = []
    for title, sl in sections:
        sid = sec_id(title)
        disp = re.sub(r'^\d+\.\s*', '', title)
        num = re.match(r'(\d+)\.', title)
        numtag = '<span class="snum">%s</span>' % num.group(1) if num else ''
        if sid == 's-short':
            body_html.append('<section class="sec short" id="%s"><h2>In short</h2>%s</section>'
                             % (sid, render_blocks(sl)))
            continue
        if sid == 's-words':
            items = []
            para = []
            for line in sl:
                m = re.match(r'^- \*\*(.+?)\*\*:\s*(.*)$', line)
                if m:
                    items.append('<div class="term"><dt>%s</dt><dd>%s</dd></div>' % (inline(m.group(1)), inline(m.group(2))))
                elif line.strip():
                    para.append(line)
            body_html.append('<section class="sec words" id="%s"><h2>Words this roadmap uses</h2>%s<dl class="glossary">%s</dl></section>'
                             % (sid, render_blocks(para), '\n'.join(items)))
            continue
        if sid == 's4':
            # split items
            chunks = re.split(r'\n(?=### RT2-\d+[a-z]? · )', '\n'.join(sl))
            head = chunks[0]
            parts = ['<section class="sec items" id="s4"><h2>%s%s</h2>%s' % (numtag, inline(disp), render_blocks(head.split('\n')))]
            for ch in chunks[1:]:
                chl = ch.split('\n')
                m = re.match(r'^### RT2-(\d+[a-z]?) · (.*)$', chl[0])
                n = m.group(1)
                item_titles[n] = m.group(2).strip()
                for ms in MILESTONES:
                    if ms[2] == n:
                        parts.append(milestone_banner(ms))
                parts.append(render_item(n, m.group(2).strip(), chl[1:]))
            parts.append('</section>')
            body_html.append('\n'.join(parts))
            continue
        if sid == 's6':
            blk = render_blocks(sl)
            # charts before the garage and Headland tables
            blk = blk.replace('<p><strong>Garage</strong> (GPU ms):</p>',
                              chart('Garage.', GARAGE, 59.0, 14.5) + '<p><strong>Garage</strong> in numbers (GPU ms):</p>', 1)
            blk = blk.replace('<p><strong>Bridge, Headland</strong> (GPU ms):</p>',
                              chart('Bridge, Headland.', HEADLAND, 49.0, 14.2) + '<p><strong>Bridge, Headland</strong> in numbers (GPU ms):</p>', 1)
            body_html.append('<section class="sec" id="s6"><h2>%s%s</h2>%s</section>' % (numtag, inline(disp), blk))
            continue
        if sid == 's8':
            body_html.append('<section class="sec appendix" id="s8"><h2>%s%s</h2><details class="appx"><summary>Show all 164 findings and the item each one goes to</summary>%s</details></section>'
                             % (numtag, inline(disp), render_blocks(sl)))
            continue
        body_html.append('<section class="sec" id="%s"><h2>%s%s</h2>%s</section>' % (sid, numtag, inline(disp), render_blocks(sl)))

    # navigation
    nav = ['<nav class="toc" aria-label="Contents"><div class="toc-title">Contents</div><ol class="toc-secs">',
           '<li><a href="#s-short">In short</a></li>', '<li><a href="#s-words">Words used</a></li>']
    names = {1: 'Where the engine stands', 2: 'The target', 3: 'Current issues', 4: 'The items',
             5: 'Milestones and decisions', 6: 'Performance budget', 7: 'Not doing', 8: 'Every finding, mapped',
             9: 'What changed', 10: 'Hard-coded values, placed', 11: 'How RageV compares'}
    for k in range(1, 12):
        nav.append('<li><a href="#s%d"><span class="tn">%d</span>%s</a>' % (k, k, names[k]))
        if k == 4:
            nav.append('<ol class="toc-ms">')
            for ms in MILESTONES:
                nav.append('<li><a class="toc-m" href="#%s">%s <span>%s</span></a><ol class="toc-items">' % (ms[0].lower(), ms[0], html.escape(ms[1])))
                for n in MS_ITEMS[ms[0]]:
                    short = re.split(r'[;:(]', item_titles.get(n, ''))[0].strip()
                    nav.append('<li><a href="#rt2-%s"><span class="tn">%s</span>%s</a></li>' % (n, n, html.escape(short)))
                nav.append('</ol></li>')
            nav.append('</ol>')
        nav.append('</li>')
    nav.append('</ol></nav>')
    nav_html = '\n'.join(nav)

    # milestone strip
    total = sum(m[4] for m in MILESTONES)
    strip = ['<div class="strip" role="img" aria-label="Eight milestones, M0 to M7, sized by their estimated sessions; RT-24 is the first sliver of M0.">']
    for m in MILESTONES:
        w = m[4] / total * 100.0
        inner = ''
        if m[0] == 'M0':
            inner = '<span class="rt24" style="width:%.1f%%" title="RT2-0: finish RT-24"></span>' % (6.5 / m[4] * 100.0)
        strip.append('<a class="seg" href="#%s" style="flex-basis:%.2f%%" title="%s %s: %s sessions">%s<span class="segl">%s</span></a>'
                     % (m[0].lower(), w, m[0], html.escape(m[1]), m[3], inner, m[0]))
    strip.append('</div>')
    legend = ['<ol class="mlist">']
    for m in MILESTONES:
        legend.append('<li><a href="#%s"><span class="mid">%s</span><span class="mname">%s</span><span class="msess">%s</span></a></li>'
                      % (m[0].lower(), m[0], html.escape(m[1]), m[3].replace('-', '–')))
    legend.append('</ol>')

    intro_text = ' '.join(l.strip() for l in intro if l.strip())
    intro_text = intro_text.strip('*')

    page = TEMPLATE.replace('%%NAV%%', nav_html).replace('%%BODY%%', '\n'.join(body_html)) \
        .replace('%%STRIP%%', '\n'.join(strip) + '\n'.join(legend)).replace('%%INTRO%%', inline(intro_text)) \
        .replace('%%TOCMOBILE%%', nav_html.replace('class="toc"', 'class="toc toc-in-details"'))
    open(OUT, 'w', encoding='utf-8', newline='\n').write(page)
    print('wrote', OUT, len(page), 'bytes')


TEMPLATE = r'''<title>RageV RT Roadmap</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@100..125,500..900&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:ital,wght@0,400;0,500;0,600;1,400&display=swap">
<style>
:root{
  color-scheme: dark;
  --ground:#131012; --surface:#1a1618; --raised:#221d20; --hair:#322a2d; --hair-2:#4a3f43;
  --text:#ede7e5; --text-2:#c9c0be; --muted:#9b908f;
  --accent:#e03131; --accent-soft:rgba(224,49,49,.13); --accent-line:rgba(224,49,49,.55);
  --code-bg:#241e21; --bar-today:#5d5155; --bar-target:#dcd4d2; --focus:#f4a3a3;
  --display:"Archivo","Arial Narrow",system-ui,sans-serif;
  --body:"IBM Plex Sans",system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  --mono:"IBM Plex Mono",ui-monospace,"Cascadia Mono",Consolas,monospace;
}
@media (prefers-color-scheme: light){
  :root:not([data-theme="dark"]){
    color-scheme: light;
    --ground:#f6f2f1; --surface:#ffffff; --raised:#fbf8f7; --hair:#e6dedc; --hair-2:#cdc2c0;
    --text:#1d1719; --text-2:#3e3538; --muted:#6c6164;
    --accent:#c52727; --accent-soft:rgba(197,39,39,.09); --accent-line:rgba(197,39,39,.5);
    --code-bg:#f0e9e8; --bar-today:#cfc4c2; --bar-target:#3e3538; --focus:#c52727;
  }
}
:root[data-theme="light"]{
  color-scheme: light;
  --ground:#f6f2f1; --surface:#ffffff; --raised:#fbf8f7; --hair:#e6dedc; --hair-2:#cdc2c0;
  --text:#1d1719; --text-2:#3e3538; --muted:#6c6164;
  --accent:#c52727; --accent-soft:rgba(197,39,39,.09); --accent-line:rgba(197,39,39,.5);
  --code-bg:#f0e9e8; --bar-today:#cfc4c2; --bar-target:#3e3538; --focus:#c52727;
}
:root[data-theme="dark"]{
  color-scheme: dark;
  --ground:#131012; --surface:#1a1618; --raised:#221d20; --hair:#322a2d; --hair-2:#4a3f43;
  --text:#ede7e5; --text-2:#c9c0be; --muted:#9b908f;
  --accent:#e03131; --accent-soft:rgba(224,49,49,.13); --accent-line:rgba(224,49,49,.55);
  --code-bg:#241e21; --bar-today:#5d5155; --bar-target:#dcd4d2; --focus:#f4a3a3;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
@media (prefers-reduced-motion: reduce){html{scroll-behavior:auto}}
body{background:var(--ground);color:var(--text);font-family:var(--body);font-size:15.5px;line-height:1.62;
  -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility}
a{color:inherit}
a:focus-visible,summary:focus-visible{outline:2px solid var(--focus);outline-offset:2px;border-radius:3px}
.shell{max-width:1340px;margin:0 auto;padding-inline:clamp(16px,3vw,36px);padding-block:28px 96px;
  display:grid;grid-template-columns:minmax(0,1fr);gap:40px}
@media (min-width:1080px){.shell{grid-template-columns:260px minmax(0,1fr);gap:56px}}
main{min-width:0}
/* ------------------------------------------------------------ hero */
.hero{padding-block:8px 28px;border-bottom:1px solid var(--hair);margin-bottom:8px}
.eyebrow{font-family:var(--mono);font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted)}
h1{font-family:var(--display);font-stretch:122%;font-weight:850;font-size:clamp(40px,7vw,76px);line-height:.95;
  letter-spacing:-.01em;margin:10px 0 14px;text-wrap:balance}
.dek{font-size:clamp(17px,2vw,20px);line-height:1.5;color:var(--text-2);max-width:62ch;margin:0 0 18px;text-wrap:pretty}
.status{font-family:var(--mono);font-size:12.5px;color:var(--muted);margin-bottom:22px}
.facts{display:flex;flex-wrap:wrap;gap:10px 28px;margin:0 0 26px;padding:0;list-style:none}
.facts li{display:flex;flex-direction:column}
.facts b{font-family:var(--display);font-stretch:112%;font-weight:750;font-size:26px;line-height:1.1;font-variant-numeric:tabular-nums}
.facts span{font-size:13px;color:var(--muted)}
.facts li.red b{color:var(--accent)}
.strip{display:flex;height:34px;gap:3px;border-radius:6px;overflow:hidden;margin-bottom:10px}
.seg{position:relative;display:flex;align-items:center;justify-content:center;background:var(--raised);
  border:1px solid var(--hair);text-decoration:none;min-width:0;overflow:hidden}
.seg:hover{background:var(--surface);border-color:var(--hair-2)}
.segl{position:relative;font-family:var(--mono);font-size:12px;color:var(--text-2);z-index:1}
.rt24{position:absolute;left:0;top:0;bottom:0;background:var(--accent)}
.mlist{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(min(210px,100%),1fr));gap:4px 18px}
.mlist a{display:flex;gap:10px;align-items:baseline;text-decoration:none;padding:3px 0;border-bottom:1px dotted var(--hair)}
.mlist a:hover .mname{color:var(--text)}
.mlist .mid{font-family:var(--mono);font-size:12px;color:var(--muted);width:2.2em}
.mlist .mname{flex:1;color:var(--text-2);font-size:14px}
.mlist .msess{font-family:var(--mono);font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums}
.stripnote{font-size:13px;color:var(--muted);margin:10px 0 0}
.stripnote i{display:inline-block;width:10px;height:10px;background:var(--accent);border-radius:2px;margin-right:6px;vertical-align:-1px}
.about{font-size:13.5px;color:var(--muted);max-width:78ch;margin:22px 0 0;line-height:1.55}
/* ------------------------------------------------------------ toc */
.toc{font-size:13.5px}
aside .toc{position:sticky;top:calc(env(safe-area-inset-top,0px) + 18px);max-height:calc(100vh - 36px);overflow:auto;padding-right:6px;
  scrollbar-width:thin}
.toc-title{font-family:var(--mono);font-size:11.5px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
.toc ol{list-style:none;margin:0;padding:0}
.toc a{display:flex;gap:8px;text-decoration:none;color:var(--text-2);padding:3px 0;line-height:1.35}
.toc a:hover{color:var(--text)}
.toc .tn{font-family:var(--mono);font-size:11.5px;color:var(--muted);min-width:1.6em;padding-top:1px}
.toc-secs>li{margin-bottom:2px}
.toc-ms{margin:4px 0 8px 12px !important;border-left:1px solid var(--hair);padding-left:10px !important}
.toc-m{font-family:var(--mono);font-size:12px;color:var(--text) !important;margin-top:6px}
.toc-m span{font-family:var(--body);color:var(--muted);font-size:12.5px}
.toc-items a{font-size:12.5px;color:var(--muted)}
.toc-items a:hover{color:var(--text)}
aside{display:none}
@media (min-width:1080px){aside{display:block}.toc-mobile{display:none}}
.toc-mobile{border:1px solid var(--hair);border-radius:8px;background:var(--surface);margin:18px 0 0}
.toc-mobile summary{cursor:pointer;padding:12px 14px;font-family:var(--mono);font-size:12.5px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-2)}
.toc-mobile .toc{padding:0 14px 14px}
/* ------------------------------------------------------------ prose */
.sec{padding-top:48px}
h2{font-family:var(--display);font-stretch:115%;font-weight:780;font-size:clamp(26px,3.2vw,34px);line-height:1.1;
  letter-spacing:-.005em;margin:0 0 18px;text-wrap:balance;display:flex;gap:14px;align-items:baseline}
.snum{font-family:var(--mono);font-weight:400;font-size:.5em;color:var(--muted);letter-spacing:.05em}
h3{font-family:var(--display);font-stretch:108%;font-weight:700;font-size:21px;line-height:1.25;margin:34px 0 12px;text-wrap:balance}
p,li{max-width:74ch;text-wrap:pretty}
p{margin:0 0 14px}
ul,ol{margin:0 0 14px;padding-left:1.25em}
li{margin:4px 0}
li>ul,li>ol{margin:4px 0 6px}
li>p{margin:8px 0 6px}
ul li::marker{color:var(--muted)}
ol li::marker{color:var(--muted);font-family:var(--mono);font-size:.9em}
strong{font-weight:600;color:var(--text)}
code{font-family:var(--mono);font-size:.86em;background:var(--code-bg);padding:.08em .34em;border-radius:4px;overflow-wrap:anywhere}
a.ref{font-family:var(--mono);font-size:.88em;text-decoration:underline;text-decoration-color:var(--hair-2);text-underline-offset:3px;white-space:nowrap}
a.ref:hover{text-decoration-color:var(--text)}
a.dref{font-family:var(--mono);font-size:.88em;font-weight:500;color:var(--accent);text-decoration:underline;text-decoration-color:var(--accent-line);text-underline-offset:3px;white-space:nowrap}
.short ul{padding-left:1.1em}
.short>ul>li{margin:10px 0}
.short p{color:var(--text-2)}
.glossary{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(300px,100%),1fr));gap:2px 32px;margin:8px 0 0}
.term{padding:9px 0;border-bottom:1px solid var(--hair)}
.term dt{font-weight:600;font-size:14.5px}
.term dd{margin:2px 0 0;color:var(--text-2);font-size:14px;line-height:1.5}
/* ------------------------------------------------------------ tables */
.tablewrap{overflow-x:auto;margin:6px 0 22px;border:1px solid var(--hair);border-radius:8px;background:var(--surface)}
table{border-collapse:collapse;width:100%;font-size:13.5px;line-height:1.45}
th,td{text-align:left;vertical-align:top;padding:9px 12px;border-bottom:1px solid var(--hair)}
th{font-family:var(--mono);font-weight:500;font-size:11.5px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);
  background:var(--raised);white-space:nowrap}
tbody tr:last-child td{border-bottom:0}
td{color:var(--text-2);font-variant-numeric:tabular-nums}
td:first-child{color:var(--text)}
tr.total td{font-weight:600;color:var(--text);background:var(--raised)}
td.mono{font-family:var(--mono);font-size:12.5px;white-space:nowrap}
table.issues td:nth-child(2){min-width:220px}
table.issues td:nth-child(5){min-width:260px}
.fate{display:inline-block;font-size:12px;line-height:1.35;padding:2px 8px;border:1px solid var(--hair-2);border-radius:999px;color:var(--text-2);white-space:nowrap}
.fate.red{border-color:var(--accent-line);color:var(--accent);background:var(--accent-soft)}
table.decisions td:nth-child(2){min-width:200px}
table.decisions td:nth-child(3){min-width:320px}
.did{font-family:var(--mono);font-weight:500;color:var(--accent)}
tr:target td{background:var(--accent-soft)}
/* ------------------------------------------------------------ items */
.mbanner{display:flex;flex-wrap:wrap;gap:6px 16px;align-items:baseline;margin:56px 0 6px;padding:14px 0 10px;border-top:2px solid var(--text);
  scroll-margin-top:16px}
.mbanner .mid{font-family:var(--mono);font-size:13px;color:var(--muted)}
.mbanner .mname{font-family:var(--display);font-stretch:115%;font-weight:760;font-size:22px;flex:1 1 240px}
.mbanner .mrange,.mbanner .msess{font-family:var(--mono);font-size:12.5px;color:var(--muted)}
.item{padding:22px 0 26px;border-bottom:1px solid var(--hair);scroll-margin-top:16px}
.item:target .item-title{text-decoration:underline;text-decoration-color:var(--hair-2);text-underline-offset:6px}
.item-head{display:grid;grid-template-columns:minmax(0,1fr);gap:4px;margin-bottom:14px}
.item-id{font-family:var(--mono);font-size:13px;color:var(--muted);letter-spacing:.04em}
.item-title{margin:0;font-size:22px;line-height:1.25}
.item-meta{display:flex;flex-wrap:wrap;gap:6px 8px;align-items:center;margin-top:6px}
.chip{display:inline-flex;align-items:center;font-family:var(--mono);font-size:11.5px;line-height:1;padding:5px 8px;
  border:1px solid var(--hair-2);border-radius:999px;color:var(--text-2);text-decoration:none;white-space:nowrap}
.chip.red{border-color:var(--accent-line);color:var(--accent);background:var(--accent-soft)}
a.chip.red:hover{border-color:var(--accent)}
.risk{display:inline-flex;align-items:center;gap:6px;font-family:var(--mono);font-size:11.5px;color:var(--muted);padding:0 4px}
.pips{display:inline-flex;gap:2px}
.pips i{display:block;width:5px;height:12px;border-radius:1px;background:var(--hair-2)}
.pips[data-level="1"] i:nth-child(-n+1),.pips[data-level="2"] i:nth-child(-n+2),.pips[data-level="3"] i{background:var(--text-2)}
.item-lead{color:var(--text-2);margin-bottom:10px}
.f{display:grid;grid-template-columns:minmax(0,1fr);gap:2px 22px;padding:10px 0}
@media (min-width:760px){.f{grid-template-columns:9.2rem minmax(0,1fr)}}
.f h4{margin:2px 0 0;font-family:var(--mono);font-weight:500;font-size:11.5px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);line-height:1.6}
.fbody>:last-child{margin-bottom:0}
.fbody ul,.fbody ol{padding-left:1.15em}
.f.small{padding:4px 0}
.f.small .fbody p{font-size:14px;color:var(--text-2)}
.item-foot{display:grid;gap:6px;margin-top:10px;padding-top:12px;border-top:1px dashed var(--hair);font-size:12.5px;color:var(--muted);line-height:1.55}
.item-foot .lbl{font-family:var(--mono);font-size:11px;letter-spacing:.08em;text-transform:uppercase;margin-right:6px;color:var(--muted)}
.item-foot code{font-size:.92em}
/* ------------------------------------------------------------ charts */
.chart{margin:10px 0 22px;padding:16px 16px 12px;border:1px solid var(--hair);border-radius:8px;background:var(--surface)}
.chart figcaption{font-size:13.5px;color:var(--text-2);margin-bottom:10px;max-width:80ch}
.legend{display:flex;gap:18px;font-size:12.5px;color:var(--muted);margin-bottom:10px;flex-wrap:wrap}
.sw{display:inline-block;width:14px;height:8px;border-radius:2px;margin-right:6px;vertical-align:0}
.sw.today{background:var(--bar-today)}.sw.target{background:var(--bar-target)}
.rows{display:grid;gap:8px}
.crow{display:grid;grid-template-columns:minmax(0,1fr);gap:4px}
@media (min-width:700px){.crow{grid-template-columns:15rem minmax(0,1fr);gap:14px;align-items:center}}
.clabel{font-size:13px;color:var(--text-2)}
.cbars{position:relative;display:grid;gap:3px}
.bar{display:flex;align-items:center;gap:8px;height:12px}
.bar .fill{display:block;flex:none;height:100%;border-radius:2px;min-width:2px}
.bar.today .fill{background:var(--bar-today)}
.bar.target .fill{background:var(--bar-target)}
.bar .val{flex:none;font-family:var(--mono);font-size:11px;color:var(--muted);white-space:nowrap;font-variant-numeric:tabular-nums}
.crow.total{padding-top:22px;margin-top:4px;border-top:1px solid var(--hair)}
.crow.total .clabel{font-weight:600;color:var(--text)}
.crow.total .bar{height:16px}
.budget{position:absolute;top:-6px;bottom:-4px;border-left:1.5px dashed var(--text-2)}
.budget span{position:absolute;top:-16px;left:4px;font-family:var(--mono);font-size:10.5px;color:var(--text-2);white-space:nowrap}
/* ------------------------------------------------------------ appendix */
.appx{border:1px solid var(--hair);border-radius:8px;background:var(--surface);padding:0 16px}
.appx summary{cursor:pointer;padding:14px 0;font-weight:500;color:var(--text)}
.appx[open]{padding-bottom:12px}
.appx .tablewrap{background:var(--ground)}
/* ------------------------------------------------------------ notes and the audit table */
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
table.audit2 td:nth-child(3){min-width:190px}
table.audit2 td:nth-child(4){min-width:300px}
table.audit tr.group td{background:var(--raised);color:var(--text);font-weight:600;padding-top:14px}
.fate.done{border-style:dashed}
footer.page{margin-top:64px;padding-top:18px;border-top:1px solid var(--hair);font-size:12.5px;color:var(--muted)}
</style>

<div class="shell">
  <aside>%%NAV%%</aside>
  <main>
    <header class="hero">
      <div class="eyebrow">RageV &middot; engine roadmap</div>
      <h1>RT roadmap</h1>
      <p class="dek">What it takes to make RageV a fast, high-fidelity, ray-tracing-first engine that can carry AAA-scale scenes. RT-24 is finished first, and the work is laid out in eight milestones, each of which leaves the engine working. The target is 60 FPS at the laptop's native 2560x1600.</p>
      <div class="status">25 September 2026 &middot; replaces RT-Series 2, rebuilt from a comparison with the top engines (<a href="#s11">section 11</a>) &middot; your 31 decisions, D0 to D30, all answered &middot; RT-24 in progress (not committed)</div>
      <ul class="facts">
        <li><b>53</b><span>items</span></li>
        <li><b>8</b><span>milestones</span></li>
        <li><b>276&ndash;360</b><span>working sessions</span></li>
        <li class="red"><b>31</b><span>decisions, all answered</span></li>
      </ul>
      %%STRIP%%
      <p class="stripnote"><i></i>RT-24, your top priority, is the red sliver at the start. Nothing else begins until it is committed. Each block's width is its estimated share of the work.</p>
      <p class="about">%%INTRO%%</p>
      <details class="toc-mobile"><summary>Contents</summary>%%TOCMOBILE%%</details>
    </header>
    %%BODY%%
    <footer class="page">Red marks your calls: RT-24, your top priority, and your 31 decisions, D0 to D30. The same content is in docs/RT-ROADMAP.md; the evidence behind section 11 is in docs/RT-ROADMAP-RESEARCH.md.</footer>
  </main>
</div>
'''

if __name__ == '__main__':
    main()
