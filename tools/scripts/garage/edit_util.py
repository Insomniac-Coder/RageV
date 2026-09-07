import io, os
BASE = 'C:/Users/ism19/Code/RageV'
CR = chr(13); LF = chr(10)

def load(rel):
    p = os.path.join(BASE, rel)
    d = io.open(p, 'r', encoding='utf-8', newline='').read()
    crlf = d.count(CR + LF); bare = d.count(LF) - crlf
    assert not (crlf and bare), '%s has MIXED line endings; refusing' % rel
    return p, d, (CR + LF if crlf else LF)

def apply(rel, edits):
    """edits: list of (label, [old lines], [new lines]) -- lines joined with the
    file's own newline, each old matched exactly once."""
    p, d, N = load(rel)
    for label, old, new in edits:
        o = N.join(old) + N
        n = N.join(new) + N
        c = d.count(o)
        assert c == 1, '%s / %s: matched %d times' % (rel, label, c)
        d = d.replace(o, n)
        print('  ok:', label)
    io.open(p, 'w', encoding='utf-8', newline='').write(d)
    crlf = d.count(CR + LF); bare = d.count(LF) - crlf
    assert not (crlf and bare), 'wrote mixed endings into %s' % rel
    print('written:', rel)
