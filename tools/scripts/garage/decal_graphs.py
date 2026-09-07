"""Print the node graph of each decal material, output-first."""
import bpy

def walk(sock, depth, seen):
    if not sock.is_linked or depth > 8:
        return
    n = sock.links[0].from_node
    pad = '   ' * depth
    extra = ''
    if n.type == 'TEX_IMAGE' and n.image:
        extra = ' image=%s alpha_mode=%s' % (n.image.name, n.image.alpha_mode)
    if n.type == 'MATH':
        extra = ' op=%s in=%s' % (n.operation, [round(i.default_value, 3) for i in n.inputs[:2]])
    if n.type == 'MIX':
        extra = ' data=%s blend=%s fac=%s' % (n.data_type, n.blend_type, round(n.inputs[0].default_value, 3))
    print('%s%s.%s <- %s [%s]%s' % (pad, sock.node.name[:18], sock.name, n.name[:22], n.type, extra))
    if n in seen:
        print(pad + '   (seen)')
        return
    seen.add(n)
    for i in n.inputs:
        if i.is_linked:
            walk(i, depth + 1, seen)
        elif i.type in ('VALUE', 'RGBA') and n.type in ('BSDF_PRINCIPLED', 'MIX_SHADER', 'EMISSION', 'BSDF_TRANSPARENT') and i.name in ('Fac', 'Alpha', 'Base Color', 'Emission Color', 'Emission Strength', 'Color', 'Strength'):
            v = i.default_value
            try: v = tuple(round(c, 3) for c in v)
            except TypeError: v = round(v, 3)
            print('%s   %s.%s = %s' % (pad, n.name[:18], i.name, v))

for name in ('scribbles', 'scribble1.001', 'scribble2.001', 'scribble3.001'):
    m = bpy.data.materials[name]
    print('=== %s  blend=%s' % (name, getattr(m, 'surface_render_method', '?')))
    out = next(n for n in m.node_tree.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output)
    walk(out.inputs['Surface'], 0, set())
print('GRAPHS-DONE')
