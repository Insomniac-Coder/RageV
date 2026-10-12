"""One-off: replace the typed-in repo path (C:/Users/ism19/Code/RageV) in RageV's tool scripts with the
repo root worked out from each script's own location, so the repo can live anywhere.

Only single-quoted code strings change; docstrings, comments and notes keep the old text. Each changed
file gets two lines after its docstring: `import os as _os` and `_RAGEV = <repo root>` (forward slashes).
"""
import ast
import io
import os
import re
import subprocess
import tokenize

ROOT = 'D:/Code_secondary/RageV'
OLD = re.compile(r'C:[/\\]+Users[/\\]+ism19[/\\]+Code[/\\]+RageV(?=[/\\]|$)', re.I)


def rewrite(text, depth):
    """(new text, replacements). A string token whose body starts with the old root becomes
    (_RAGEV + <the same literal without the root>)."""
    edits = []
    for tok in tokenize.generate_tokens(io.StringIO(text).readline):
        if tok.type != tokenize.STRING:
            continue
        m = re.match(r"([rRbBuU]{0,2})('|\")", tok.string)
        if not m or tok.string[len(m.group(0))] == m.group(2):  # triple-quoted: leave
            continue
        prefix, quote = m.groups()
        body = tok.string[len(m.group(0)):-1]
        root = OLD.match(body)
        if root:
            edits.append((tok.start, tok.end, '(_RAGEV + %s%s%s%s)' % (prefix, quote, body[root.end():], quote)))
    if not edits:
        return text, 0
    lines = text.splitlines(keepends=True)
    for (sr, sc), (er, ec), new in reversed(edits):  # single-quoted tokens never span lines
        lines[sr - 1] = lines[sr - 1][:sc] + new + lines[sr - 1][ec:]
    tree = ast.parse(text)
    after = 0
    for i, node in enumerate(tree.body):  # past the docstring and any __future__ import
        doc = (i == 0 and isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant)
               and isinstance(node.value.value, str))
        if doc or (isinstance(node, ast.ImportFrom) and node.module == '__future__'):
            after = node.end_lineno
        else:
            break
    nl = '\r\n' if '\r\n' in text else '\n'
    up = ', '.join(["'..'"] * depth)
    lines[after:after] = [
        'import os as _os' + nl,
        "_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), %s)).replace('\\\\', '/')"
        '  # the repo root, wherever it lives%s' % (up, nl)]
    return ''.join(lines), len(edits)


def main():
    files = subprocess.run(['git', 'grep', '-Il', '-i', '-E', r'Users[/\\]+ism19[/\\]+Code[/\\]+ragev', '--',
                            'tools/*.py'], cwd=ROOT, capture_output=True, text=True).stdout.split()
    total = changed = 0
    for rel in files:
        path = os.path.join(ROOT, rel)
        text = io.open(path, encoding='utf-8', newline='').read()
        new, n = rewrite(text, rel.count('/'))
        if not n:
            print('  only in text (left as is):', rel)
            continue
        compile(new, rel, 'exec')
        io.open(path, 'w', encoding='utf-8', newline='').write(new)
        total += n
        changed += 1
    print('%d files changed, %d strings rewritten' % (changed, total))


if __name__ == '__main__':
    main()
