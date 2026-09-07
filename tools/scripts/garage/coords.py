import bpy

WANT = ('concrete', 'concrete.006', 'wet road.001', 'Material.014',
        'metalic shader.001', 'scribble1.001', 'scribbles', 'ladders mat')

print('=== which coordinate source each texture uses ===')
for m in bpy.data.materials:
    if m.name not in WANT or not m.use_nodes:
        continue
    print('[%s]' % m.name)
    for n in m.node_tree.nodes:
        if n.type != 'TEX_IMAGE':
            continue
        img = n.image.name if n.image else '(none)'
        src = 'UV (default)'
        scale = rot = None
        v = n.inputs.get('Vector')
        if v and v.is_linked:
            up = v.links[0].from_node
            if up.type == 'MAPPING':
                scale = tuple(round(x, 3) for x in up.inputs['Scale'].default_value)
                rot = tuple(round(x, 3) for x in up.inputs['Rotation'].default_value)
                vv = up.inputs['Vector']
                if vv.is_linked:
                    src_node = vv.links[0].from_node
                    if src_node.type == 'TEX_COORD':
                        src = vv.links[0].from_socket.name + '  <- TexCoord'
                    elif src_node.type == 'UVMAP':
                        src = 'UVMap:' + (src_node.uv_map or 'active')
                    else:
                        src = src_node.type
            elif up.type == 'TEX_COORD':
                src = v.links[0].from_socket.name + '  <- TexCoord'
            elif up.type == 'UVMAP':
                src = 'UVMap:' + (up.uv_map or 'active')
            else:
                src = up.type
        print('   %-46s source=%-22s scale=%s rot=%s'
              % (img[:46], src, scale, rot))
    # what the mesh's own UVs look like
print()
print('=== UV layers on the objects that use them ===')
seen = set()
for o in bpy.data.objects:
    if o.type != 'MESH':
        continue
    for s in o.material_slots:
        if s.material and s.material.name in WANT and s.material.name not in seen:
            seen.add(s.material.name)
            uvs = [l.name for l in o.data.uv_layers]
            print('   %-20s on %-24s uv layers=%s' % (s.material.name, o.name[:24], uvs))
