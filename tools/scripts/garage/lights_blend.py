import bpy, math

print('=== LIGHTS ===')
for o in bpy.data.objects:
    if o.type != 'LIGHT':
        continue
    L = o.data
    loc = tuple(round(v, 3) for v in o.matrix_world.translation)
    col = tuple(round(c, 3) for c in L.color)
    extra = ''
    if L.type == 'AREA':
        extra = 'size=%.3f x %.3f shape=%s' % (L.size, getattr(L, 'size_y', L.size), L.shape)
    elif L.type == 'SPOT':
        extra = 'cone=%.1f blend=%.2f' % (math.degrees(L.spot_size), L.spot_blend)
    elif L.type == 'POINT':
        extra = 'radius=%.3f' % L.shadow_soft_size
    print('  %-26s %-6s loc=%-28s energy=%-9.2f color=%-20s %s'
          % (o.name[:26], L.type, str(loc), L.energy, str(col), extra))

print()
print('=== CAMERAS ===')
for o in bpy.data.objects:
    if o.type != 'CAMERA':
        continue
    C = o.data
    loc = tuple(round(v, 3) for v in o.matrix_world.translation)
    rot = tuple(round(math.degrees(a), 2) for a in o.matrix_world.to_euler())
    fov = math.degrees(C.angle)
    print('  %-22s loc=%-28s rot=%-26s lens=%.1fmm  fov=%.1f  sensor=%.1f'
          % (o.name[:22], str(loc), str(rot), C.lens, fov, C.sensor_width))
sc = bpy.context.scene
print('  scene camera:', sc.camera.name if sc.camera else None,
      ' render %dx%d' % (sc.render.resolution_x, sc.render.resolution_y),
      ' engine', sc.render.engine)
if hasattr(sc, 'view_settings'):
    print('  view transform:', sc.view_settings.view_transform,
          'look:', sc.view_settings.look, 'exposure:', round(sc.view_settings.exposure, 3),
          'gamma:', round(sc.view_settings.gamma, 3))

print()
print('=== EMISSIVE MATERIALS (the strips) ===')
for m in bpy.data.materials:
    if not m.use_nodes or not m.node_tree:
        continue
    for n in m.node_tree.nodes:
        if n.type == 'EMISSION':
            c = n.inputs['Color'].default_value
            s = n.inputs['Strength'].default_value
            print('  %-24s EMISSION colour=%s strength=%.3f'
                  % (m.name[:24], tuple(round(v, 3) for v in c), s))
        elif n.type == 'BSDF_PRINCIPLED' and 'Emission Strength' in n.inputs:
            s = n.inputs['Emission Strength'].default_value
            if s and s > 0:
                key = 'Emission Color' if 'Emission Color' in n.inputs else 'Emission'
                c = n.inputs[key].default_value
                print('  %-24s PRINCIPLED emission=%s strength=%.3f'
                      % (m.name[:24], tuple(round(v, 3) for v in c), s))

print()
print('=== WORLD ===')
w = bpy.context.scene.world
if w and w.use_nodes:
    for n in w.node_tree.nodes:
        if n.type == 'BACKGROUND':
            print('  background colour=%s strength=%.4f'
                  % (tuple(round(v, 3) for v in n.inputs['Color'].default_value),
                     n.inputs['Strength'].default_value))
