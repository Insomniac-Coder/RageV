"""Mark every mesh under the garage root Static in showroom.rage.

A garage is architecture: every piece of it is still, and the bake only sees
what says so. The car keeps whatever the studio scene gave it. Split out of
fix1.py, whose material patches belong to the first FBX import and nothing
since.
"""
import sys, io
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
import scene_util as S

SCENE = r'C:\Users\ism19\Code\RageV\SampleProject\assets\scenes\showroom.rage'

head, blocks, N = S.load(SCENE)
roots = [S.eid(b) for b in blocks if S.tag(b).startswith('underground_garage')
         and S.parent(b) is None]
assert len(roots) == 1, 'garage roots found: %d' % len(roots)
garage_ids = set(roots)
changed = True
while changed:                       # the subtree, however deep
    changed = False
    for b in blocks:
        p = S.parent(b)
        if p in garage_ids and S.eid(b) not in garage_ids:
            garage_ids.add(S.eid(b))
            changed = True

marked = 0
for b in blocks:
    if S.eid(b) not in garage_ids:
        continue
    for i, ln in enumerate(b):
        if ln.strip() != 'MeshComponent:':
            continue
        # the flag may sit anywhere in the component block, not only on the
        # line after -- inserting a second one left two, and the parser reads
        # the last
        j = i + 1
        found = False
        while j < len(b) and b[j].startswith('      '):
            if b[j].strip().startswith('Static:'):
                found = True
                if 'false' in b[j]:
                    b[j] = '      Static: true'
                    marked += 1
            j += 1
        if not found:
            b.insert(i + 1, '      Static: true')
            marked += 1
        break

body = []
for b in blocks:
    body.extend(b)
io.open(SCENE, 'w', encoding='utf-8', newline='').write(head + N.join(body) + N)
print('scene: %d garage meshes marked Static (%d entities in the subtree)' % (marked, len(garage_ids)))
