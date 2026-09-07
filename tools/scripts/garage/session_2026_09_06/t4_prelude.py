"""Prelude for the second half of the T4 patch: the same helpers, tolerant
of the mixed line endings Renderer3D.cpp carries (some lines CRLF, some
LF): each match is tried with both."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def _variants(text, nl):
    seen = []
    for cand in (text.replace('\n', nl), text.replace('\n', '\n'), text.replace('\n', '\r\n')):
        if cand not in seen:
            seen.append(cand)
    return seen
def rep(s, nl, old, new, count=1):
    for o in _variants(old, nl):
        if s.count(o) == count:
            n = new.replace('\n', '\r\n' if '\r\n' in o or (('\n' not in o) and nl == '\r\n') else '\n')
            return s.replace(o, n)
    raise AssertionError((s.count(old.replace('\n', nl)), old[:70]))
def span(s, nl, start, end_line, new):
    a = -1
    for st in _variants(start, nl):
        a = s.find(st)
        if a >= 0:
            break
    assert a >= 0, start[:60]
    b = -1
    for en in _variants(end_line, nl):
        b = s.find(en, a)
        if b >= 0:
            break
    assert b >= 0, end_line[:60]
    b = s.find('\n', b) + 1
    return s[:a] + new.replace('\n', nl) + s[b:]
