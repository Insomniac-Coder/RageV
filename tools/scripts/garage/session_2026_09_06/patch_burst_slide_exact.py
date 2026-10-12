"""BURST_SLIDE: a prefix written "=tag" must match the whole tag, so a
model's root does not take its parts along twice."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
p = (_RAGEV + '/tools/scripts/garage/burst.py')
s = open(p, encoding='utf-8').read()
old = "            if tag and any(tag.group(1).strip().startswith(pfx) for pfx in prefixes):\n                assert 'NativeScriptComponent' not in part, tag.group(1)"
assert s.count(old) == 2, s.count(old)   # the switch hook and the slide hook share the line; both may use "=tag"
new = ("            if tag and any((tag.group(1).strip() == pfx[1:]) if pfx.startswith('=')\n"
       "                           else tag.group(1).strip().startswith(pfx) for pfx in prefixes):\n"
       "                assert 'NativeScriptComponent' not in part, tag.group(1)")
s = s.replace(old, new)
s = s.replace("    # one of the prefixes: the object drives along world X while the camera,",
              "    # one of the prefixes (a prefix written \"=tag\" must match the whole tag,\n"
              "    # so a model's root does not take its parts along twice): the object\n"
              "    # drives along world X while the camera,")
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('BURST_SLIDE / BURST_SWITCH: exact match with a leading =')
