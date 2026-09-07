"""Everything the PBR bake needs to know about the .blend, in one run.

Camera and object bounds in glTF/engine axes (x, y=up, z=-blender_y, what
export_yup does); per material which Principled inputs are linked and to
what; per object its mesh data, modifiers, area and material slots.
"""
import bpy, math
from mathutils import Vector

def to_gl(v):
    return (round(v.x, 3), round(v.z, 3), round(-v.y, 3))

sc = bpy.context.scene
cam = sc.camera
print('CAMERA', cam.name, 'loc_gl=', to_gl(cam.matrix_world.translation))
fwd = cam.matrix_world.to_quaternion() @ Vector((0, 0, -1))
print('   forward_gl=', to_gl(fwd), ' lens=%.2f sensor=%.1f fit=%s angle=%.2f'
      % (cam.data.lens, cam.data.sensor_width, cam.data.sensor_fit, math.degrees(cam.data.angle)))
print('   render %dx%d  view=%s look=%s exposure=%.2f'
      % (sc.render.resolution_x, sc.render.resolution_y, sc.view_settings.view_transform,
         sc.view_settings.look, sc.view_settings.exposure))
print('   bake normal space=%s g=%s' % (sc.render.bake.normal_space, sc.render.bake.normal_g))

def src_desc(sock):
    if not sock.is_linked:
        return None
    n = sock.links[0].from_node
    return '%s%s' % (n.type, ('(' + n.image.name[:30] + ')') if n.type == 'TEX_IMAGE' and n.image else '')

print()
print('MATERIALS')
for m in sorted(bpy.data.materials, key=lambda x: x.name):
    if not m.use_nodes or not m.node_tree:
        print('  %-28s NO-NODES' % m.name); continue
    outn = next((n for n in m.node_tree.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output), None)
    surf = outn.inputs['Surface'].links[0].from_node if outn and outn.inputs['Surface'].is_linked else None
    if surf is None or surf.type != 'BSDF_PRINCIPLED':
        print('  %-28s surface=%s' % (m.name, surf.type if surf else None))
        continue
    b = surf
    parts = []
    for key in ('Base Color', 'Roughness', 'Metallic', 'Normal', 'Alpha', 'Emission Color'):
        s = b.inputs.get(key)
        if s is None: continue
        d = src_desc(s)
        if d:
            parts.append('%s<-%s' % (key.replace(' ', ''), d))
        else:
            v = s.default_value
            try: v = tuple(round(c, 3) for c in v)
            except TypeError: v = round(v, 3)
            parts.append('%s=%s' % (key.replace(' ', ''), v))
    es = b.inputs['Emission Strength'].default_value if 'Emission Strength' in b.inputs else None
    users = [o.name for o in bpy.data.objects if o.type == 'MESH' and any(sl.material == m for sl in o.material_slots)]
    print('  %-28s strength=%-6s users=%d %s' % (m.name, es, len(users), ' '.join(parts)))
    print('        blend=%s users=%s' % (m.blend_method if hasattr(m, 'blend_method') else '?', users[:6]))

print()
print('OBJECTS')
seen = {}
for o in sorted(bpy.data.objects, key=lambda x: x.name):
    if o.type != 'MESH':
        print('  %-26s type=%s' % (o.name, o.type)); continue
    pts = [o.matrix_world @ Vector(c) for c in o.bound_box]
    lo = tuple(round(min(to_gl(p)[i] for p in pts), 2) for i in range(3))
    hi = tuple(round(max(to_gl(p)[i] for p in pts), 2) for i in range(3))
    area = sum(p.area for p in o.data.polygons) * abs(o.scale.x * o.scale.y * o.scale.z) ** (2 / 3.0)
    mods = [md.type for md in o.modifiers]
    mats = [sl.material.name if sl.material else None for sl in o.material_slots]
    uvs = [l.name for l in o.data.uv_layers]
    dup = seen.get(o.data.name)
    seen.setdefault(o.data.name, o.name)
    print('  %-26s data=%-20s faces=%-6d area=%-8.1f lo=%s hi=%s mods=%s uvs=%s mats=%s%s'
          % (o.name, o.data.name, len(o.data.polygons), area, lo, hi, mods, uvs, mats,
             ('  DUP-OF ' + dup) if dup else ''))
print('SURVEY-DONE')
