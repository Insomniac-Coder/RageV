import sys, io, re, glob, os
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
import scene_util as S

D = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage'
SCENE = r'C:\Users\ism19\Code\RageV\SampleProject\assets\scenes\showroom.rage'


def patch(name, fields):
    for p in glob.glob(os.path.join(D, '*.rmat')):
        m = re.match(r'^underground_garage_\d+_(.*)\.rmat$', os.path.basename(p))
        if not m or m.group(1) != name:
            continue
        d = io.open(p, 'r', encoding='utf-8', newline='').read()
        for k, v in fields.items():
            d = re.sub(r'(?m)^(%s:).*$' % k, r'\1 ' + v, d, count=1)
        io.open(p, 'w', encoding='utf-8', newline='').write(d)
        return True
    raise AssertionError('no material named ' + name)


# The shell carries a *tiling* concrete albedo, and at [1, 1] one 2K image was
# stretched over a room 47 m across -- a handful of texels magnified into the
# soft blotches on the walls. Tiled at roughly four metres, and the tint goes
# back near white so the map's own grey is what shows: the walls read dark in
# the reference because almost no light reaches them, not because the concrete
# is black.
patch('Material.014', dict(BaseColor='[0.52, 0.54, 0.58, 1]',
                           Tiling='[11, 13]', Roughness='0.9'))
# The two ceiling slabs and the columns are the same concrete, untextured.
patch('concrete', dict(BaseColor='[0.2, 0.205, 0.215, 1]'))
patch('concrete.006', dict(BaseColor='[0.185, 0.19, 0.2, 1]'))
# Undo the near-mirror diagnostic.
patch('wet road.001', dict(Roughness='0.11'))
print('materials: shell tiled, concrete lifted, floor back to 0.11')

# --- Static, which the import did not set ----------------------------------
# A garage is architecture: every piece of it is still, and the bake only sees
# what says so. The car keeps whatever the studio scene gave it.
head, blocks, N = S.load(SCENE)
garage_ids = set()
root = None
for b in blocks:
    if S.tag(b) == 'underground_garage':
        root = S.eid(b)
garage_ids.add(root)
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
print('scene: %d garage meshes marked Static' % marked)
