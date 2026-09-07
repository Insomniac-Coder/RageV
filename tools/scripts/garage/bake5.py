"""Bake Blender's finished lighting into a texture per object.

The file renders correctly in Cycles; the engine is not Cycles. So instead
of carrying materials across and hoping the engine's lighting lands in the
same place, bake what Cycles actually produces -- albedo under the two lights
and the emissive tubes, bounces and all -- into an image on each object's own
UVs, and let the engine show that image self-lit. The room becomes a
photograph of the render; the car stays live and still reflects in the floor.

Per object because the scene projects its concrete in object space, so no two
objects share a layout. Sized by surface area so the 47 m floor is not asked
to share 1024 texels with a light fitting.
"""
import bpy, os, bmesh
from mathutils import Vector

OUT = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage_lit'
os.makedirs(OUT, exist_ok=True)

sc = bpy.context.scene
sc.render.engine = 'CYCLES'
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.get_devices()
    prefs.compute_device_type = 'OPTIX'
    for d in prefs.devices:
        d.use = True
    sc.cycles.device = 'GPU'
except Exception as e:
    print('gpu', e)
sc.cycles.samples = 48
try:
    sc.cycles.use_denoising = True
except Exception:
    pass
bs = sc.render.bake
bs.use_pass_direct = True
bs.use_pass_indirect = True
bs.use_pass_color = True
bs.use_pass_emit = True
bs.margin = 8
bs.use_selected_to_active = False
bs.use_clear = True


def select_only(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


# the shell faces the room
cube = bpy.data.objects.get('Cube')
if cube:
    select_only(cube)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=True)
    bpy.ops.object.mode_set(mode='OBJECT')


def area_of(o):
    return sum(p.area for p in o.data.polygons) * abs(o.scale.x * o.scale.y * o.scale.z) ** (2 / 3.0)


def unwrap_for_bake(o, boxy):
    me = o.data
    # The artist's UV layer stays until after the bake: the floor's puddle and
    # grunge textures read it by name, and taking it away first left that
    # material evaluating to nothing -- a black bake. It is dropped in the
    # rewire step below, once the picture is safely in the image.
    if 'bake_uv' not in me.uv_layers:
        me.uv_layers.new(name='bake_uv')
    me.uv_layers.active = me.uv_layers['bake_uv']
    me.uv_layers['bake_uv'].active_render = True
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    if boxy:
        # six faces of a room: cube projection would stack opposite faces on
        # the same texels, so smart project with a wide angle limit instead
        bpy.ops.uv.smart_project(angle_limit=1.4, island_margin=0.01, scale_to_bounds=True)
    else:
        bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.02, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')


def bake_lit(o, res):
    select_only(o)
    boxy = o.name in ('Cube',)
    unwrap_for_bake(o, boxy)
    safe = ''.join(c if c.isalnum() or c in '._-' else '_' for c in o.name)
    img = bpy.data.images.new('lit_%s' % safe, res, res, alpha=False, float_buffer=False)
    nodes = []
    for slot in o.material_slots:
        m = slot.material
        if m is None:
            continue
        n = m.node_tree.nodes.new('ShaderNodeTexImage')
        n.image = img
        n.select = True
        m.node_tree.nodes.active = n
        nodes.append((m, n))
    if not nodes:
        return False
    try:
        bpy.ops.object.bake(type='COMBINED')
    except Exception as e:
        print('   bake failed %s: %s' % (o.name, e))
        for m, n in nodes:
            m.node_tree.nodes.remove(n)
        return False
    path = os.path.join(OUT, 'lit_%s.png' % safe)
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    px = img.pixels[:]
    mean = sum(px[0::4]) / max(1, len(px) // 4)
    print('   %-26s res=%-5d mean=%.3f' % (o.name[:26], res, mean))
    for i, (m, n) in enumerate(nodes):
        mm = m.copy()
        mm.name = 'lit_%s_%d' % (safe, i)
        o.material_slots[i].material = mm
        nt = mm.node_tree
        for node in list(nt.nodes):
            nt.nodes.remove(node)
        outn = nt.nodes.new('ShaderNodeOutputMaterial')
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        tex = nt.nodes.new('ShaderNodeTexImage')
        tex.image = img
        # the picture goes out as emission, with nothing for the engine to
        # relight: black base, and the roughness the surface really has
        b.inputs['Base Color'].default_value = (0, 0, 0, 1)
        b.inputs['Metallic'].default_value = 0.0
        b.inputs['Roughness'].default_value = 0.25 if o.name == 'Plane.016' else 0.8
        for key in ('Emission Color', 'Emission'):
            if key in b.inputs:
                nt.links.new(tex.outputs['Color'], b.inputs[key])
                break
        if 'Emission Strength' in b.inputs:
            b.inputs['Emission Strength'].default_value = 1.0
        nt.links.new(b.outputs['BSDF'], outn.inputs['Surface'])
        m.node_tree.nodes.remove(n)
    return True


meshes = [o for o in bpy.data.objects if o.type == 'MESH' and len(o.data.materials)]
meshes.sort(key=area_of, reverse=True)
seen_mesh = set()
done = 0
for o in meshes:
    if o.data.name in seen_mesh:
        continue           # linked duplicates share the mesh: baked once, in
    seen_mesh.add(o.data.name)   # object space, which is the same for all
    a = area_of(o)
    res = 2048 if a > 400 else 1024 if a > 40 else 512 if a > 4 else 256
    if bake_lit(o, res):
        done += 1
print('LIT-BAKED %d meshes' % done)

gl = os.path.join(OUT, 'underground_garage_lit.gltf')
bpy.ops.object.select_all(action='DESELECT')
for o in bpy.data.objects:
    if o.type == 'MESH':
        o.select_set(True)
bpy.ops.export_scene.gltf(filepath=gl, export_format='GLTF_SEPARATE',
                          use_selection=True, export_yup=True,
                          export_materials='EXPORT', export_image_format='AUTO',
                          export_cameras=False, export_lights=False,
                          export_animations=False)
print('EXPORTED', gl)
