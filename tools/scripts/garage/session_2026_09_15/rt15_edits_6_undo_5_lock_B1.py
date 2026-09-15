# -*- coding: utf-8 -*-
"""Undo rt15_edits_5 exactly (the owner locked STATE B1, 2026-09-15 evening), and prove it: the
trace shader is rebuilt from HEAD through edit scripts 2, 3 and 4 in a scratch copy and must
equal the undone file byte for byte."""
import importlib.util, io, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = r'C:\Users\ism19\Code\RageV'
REL = 'RageVEditor/assets/shaders/reflection_trace.rvshader'
CRLF, LF = '\r\n', '\n'


def load(name):
    """The (old, new) pairs an edit script applies to reflection_trace, read from its source
    without running it: each script builds a list per file through apply(rel, subs)."""
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    src = io.open(spec.origin, encoding='utf-8').read()
    # run the script's module with apply() replaced by a recorder
    recorded = {}
    ns = {'__name__': 'recorder', '__file__': spec.origin}
    code = src.replace("def apply(rel, subs", "def _apply_unused(rel, subs")
    code = code.replace("def apply(rel, subs, by_line=None", "def _apply_unused(rel, subs, by_line=None")
    ns['apply'] = lambda rel, subs, by_line=None: recorded.setdefault(rel, []).extend(subs)
    exec(compile(code, spec.origin, 'exec'), ns)
    return recorded


def substitute(text, subs, reverse=False):
    for old, new in subs:
        if reverse:
            old, new = new, old
        n = text.count(old)
        if n != 1:
            sys.exit('a substitution matched %d times: %r' % (n, old[:90]))
        text = text.replace(old, new)
    return text


# 1. undo edit 5 on the file as it stands
path = os.path.join(ROOT, REL)
raw = io.open(path, encoding='utf-8', newline='').read()
crlf = CRLF in raw
text = substitute(raw.replace(CRLF, LF), load('rt15_edits_5_moving_rays_sequence')[REL], reverse=True)
io.open(path, 'w', encoding='utf-8', newline='').write(text.replace(LF, CRLF) if crlf else text)
print('edit 5 undone on', REL)

# 2. rebuild B1's trace from HEAD through edits 2, 3, 4 (edit 1 touched only the include)
head = subprocess.run(['git', 'show', 'HEAD:' + REL], cwd=ROOT, capture_output=True, check=True).stdout.decode('utf-8').replace(CRLF, LF)
rebuilt = head
for script, key in (('rt15_edits_2_hit_specular_rest', 'reflection_trace.rvshader'),
                    ('rt15_edits_3_hit_probe', 'reflection_trace.rvshader'),
                    ('rt15_edits_4_streaks_and_moving_rays', REL)):
    subs = load(script)[key]
    rebuilt = substitute(rebuilt, subs)
now = io.open(path, encoding='utf-8', newline='').read().replace(CRLF, LF)
print('rebuilt B1 trace equals the undone file byte for byte:', rebuilt == now)
if rebuilt != now:
    sys.exit('B1 reconstruction differs')
