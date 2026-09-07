import bpy, os
from collections import Counter

print('=== BLEND INSPECTION ===')
print('materials:', len(bpy.data.materials))
print('images:', len(bpy.data.images))
print()
print('--- packed images ---')
for im in bpy.data.images:
    packed = im.packed_file is not None
    print('  %-46s %-10s %sx%s  src=%s' % (im.name[:46], 'PACKED' if packed else 'external',
                                           im.size[0], im.size[1], im.source))
print()
print('--- the materials that cover the room ---')
WANT = ('concrete', 'concrete.006', 'Material.014', 'wet road.001', 'light', 'light2',
        'Surface', 'metal Surface', 'Material.004', 'Material.003')
for m in bpy.data.materials:
    if m.name not in WANT:
        continue
    print('  [%s]  use_nodes=%s' % (m.name, m.use_nodes))
    if not m.use_nodes or not m.node_tree:
        print('      no node tree; diffuse', tuple(round(c, 3) for c in m.diffuse_color))
        continue
    kinds = Counter(n.type for n in m.node_tree.nodes)
    print('      nodes:', dict(kinds))
    for n in m.node_tree.nodes:
        if n.type == 'TEX_IMAGE':
            print('      IMAGE:', n.image.name if n.image else '(none)')
        elif n.type in ('TEX_NOISE', 'TEX_VORONOI', 'TEX_MUSGRAVE', 'TEX_WAVE',
                        'TEX_MAGIC', 'TEX_BRICK', 'TEX_GRADIENT', 'TEX_CHECKER'):
            print('      PROCEDURAL:', n.type)
        elif n.type == 'BSDF_PRINCIPLED':
            for inp in ('Base Color', 'Roughness', 'Metallic'):
                if inp in n.inputs:
                    s = n.inputs[inp]
                    v = s.default_value
                    try:
                        v = tuple(round(c, 3) for c in v)
                    except TypeError:
                        v = round(v, 3)
                    print('      Principled %-11s linked=%-5s value=%s'
                          % (inp, s.is_linked, v))
print()
print('--- every material: does it reach an image? ---')
for m in sorted(bpy.data.materials, key=lambda x: x.name):
    imgs = []
    if m.use_nodes and m.node_tree:
        imgs = [n.image.name for n in m.node_tree.nodes
                if n.type == 'TEX_IMAGE' and n.image]
    proc = []
    if m.use_nodes and m.node_tree:
        proc = sorted(set(n.type for n in m.node_tree.nodes if n.type.startswith('TEX_')
                          and n.type != 'TEX_IMAGE'))
    print('  %-34s images=%-2d %-38s procedural=%s'
          % (m.name[:34], len(imgs), ','.join(imgs)[:38], ','.join(proc) or '-'))
