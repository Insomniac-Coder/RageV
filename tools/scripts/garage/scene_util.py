import io, re
def load(path):
    d = io.open(path,'r',encoding='utf-8',newline='').read()
    N = '\r\n' if d.count('\r\n') else '\n'
    head, sep, rest = d.partition('Entities:' + N)
    assert sep, 'no Entities: block'
    lines = rest.split(N)
    blocks, cur = [], None
    for ln in lines:
        if ln.startswith('  - EntityID:'):
            if cur is not None: blocks.append(cur)
            cur = [ln]
        elif cur is not None:
            cur.append(ln)
    if cur is not None: blocks.append(cur)
    # strip trailing empties from the last block
    while blocks and blocks[-1] and blocks[-1][-1] == '':
        blocks[-1].pop()
    return head + 'Entities:' + N, blocks, N
def tag(b):
    for ln in b:
        m = re.match(r'\s+Tag: (.*)$', ln)
        if m: return m.group(1)
    return '?'
def eid(b):
    return re.match(r'  - EntityID: (\d+)', b[0]).group(1)
def comps(b):
    return [re.match(r'    ([A-Za-z]+Component):', ln).group(1)
            for ln in b if re.match(r'    [A-Za-z]+Component:', ln)]
def parent(b):
    for i, ln in enumerate(b):
        if ln.strip() == 'RelationshipComponent:':
            for j in range(i+1, min(i+4, len(b))):
                m = re.match(r'\s+Parent: (\d+)', b[j])
                if m: return m.group(1)
    return None
