import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import os, sys, collections

ROOT = (_RAGEV + r'')
CODE = {'.cpp': 'C++', '.cc': 'C++', '.cxx': 'C++', '.c': 'C', '.h': 'C/C++ header', '.hpp': 'C/C++ header',
        '.hh': 'C/C++ header', '.inl': 'C/C++ header', '.cs': 'C#', '.py': 'Python',
        '.rvshader': 'Shaders', '.glsl': 'Shaders', '.hlsl': 'Shaders', '.vert': 'Shaders', '.frag': 'Shaders',
        '.comp': 'Shaders', '.cmake': 'CMake'}
SKIP_DIRS = {'build', '.git', 'bin', 'obj', '__pycache__', 'node_modules'}


def count(path):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return 0, 0
    lines = data.split(b'\n')
    total = len(lines) - (1 if data.endswith(b'\n') else 0)
    nonblank = sum(1 for l in lines if l.strip())
    return total, nonblank


def walk(top, exclude_vendor):
    by_lang = collections.defaultdict(lambda: [0, 0, 0])
    for d, dirs, files in os.walk(top):
        rel = os.path.relpath(d, ROOT).replace('\\', '/')
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith('.')]
        if exclude_vendor and (rel == 'RageV/vendor' or rel.startswith('RageV/vendor/')):
            dirs[:] = []
            continue
        # generated / captured output living in the project folders, not source
        if rel.startswith('SampleProject/bin') or rel.startswith('Knockdown/bin'):
            dirs[:] = []
            continue
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            lang = CODE.get(ext) or ('CMake' if f == 'CMakeLists.txt' else None)
            if not lang:
                continue
            t, n = count(os.path.join(d, f))
            by_lang[lang][0] += 1
            by_lang[lang][1] += t
            by_lang[lang][2] += n
    return by_lang


def show(title, by_lang):
    print(title)
    tf = tt = tn = 0
    for lang, (f, t, n) in sorted(by_lang.items(), key=lambda kv: -kv[1][1]):
        print('  %-14s %6d files %10d lines %10d non-blank' % (lang, f, t, n))
        tf += f; tt += t; tn += n
    print('  %-14s %6d files %10d lines %10d non-blank' % ('TOTAL', tf, tt, tn))


own = walk(ROOT, True)
vendor = walk(os.path.join(ROOT, 'RageV', 'vendor'), False)
show('Without third-party (RageV/vendor excluded):', own)
show('Third-party only (RageV/vendor):', vendor)
allc = collections.defaultdict(lambda: [0, 0, 0])
for src in (own, vendor):
    for k, v in src.items():
        for i in range(3):
            allc[k][i] += v[i]
show('With third-party:', allc)
