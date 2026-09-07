"""bake3 plus the floor's roughness.

The floor's look in the reference is mostly reflection: a wet, low-roughness
surface with the strips mirrored in it, and the diffuse slab pattern sits
underneath, barely visible. Its roughness map is object-projected like the
colour was, so after export it lands on the wrong UVs and reads as noise --
mostly rough -- and the diffuse grid shows through instead. Bake the
roughness to the same top-face UVs as the colour.
"""
import bpy, os, bmesh
from mathutils import Vector

OUT = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage_baked'
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
sc.cycles.samples = 16
bs = sc.render.bake
bs.use_pass_direct = False
bs.use_pass_indirect = False
bs.use_pass_color = True
bs.margin = 16
bs.use_selected_to_active = False
bs.use_clear = True


def select_only(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


cube = bpy.data.objects.get('Cube')
if cube:
    select_only(cube)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    print('shell normals now face inward')


def prepare_uv(o, top_only):
    me = o.data
    if 'bake_uv' not in me.uv_layers:
        me.uv_layers.new(name='bake_uv')
    me.uv_layers.active = me.uv_layers['bake_uv']
    me.uv_layers['bake_uv'].active_render = True
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.cube_project(cube_size=1.0, correct_aspect=True, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    if top_only:
        bm = bmesh.new(); bm.from_mesh(me)
        uv = bm.loops.layers.uv['bake_uv']
        up = Vector((0.0, 0.0, 1.0))
        for f in bm.faces:
            if f.normal.dot(up) < 0.5:
                for l in f.loops:
                    l[uv].uv = l[uv].uv + Vector((10.0, 10.0))
        bm.to_mesh(me); bm.free()


def bake_pass(o, res, bake_type, tag):
    safe = ''.join(c if c.isalnum() or c in '._-' else '_' for c in o.name)
    img = bpy.data.images.new('rb_%s_%s' % (safe, tag), res, res, alpha=False)
    if bake_type != 'DIFFUSE':
        img.colorspace_settings.name = 'Non-Color'
    nodes = []
    for slot in o.material_slots:
        m = slot.material
        if m is None:
            continue
        n = m.node_tree.nodes.new('ShaderNodeTexImage')
        n.image = img; n.select = True
        m.node_tree.nodes.active = n
        nodes.append((m, n))
    if not nodes:
        return None, []
    bpy.ops.object.bake(type=bake_type)
    path = os.path.join(OUT, 'rb_%s_%s.png' % (safe, tag))
    img.filepath_raw = path; img.file_format = 'PNG'; img.save()
    px = img.pixels[:]
    mean = sum(px[0::4]) / max(1, len(px) // 4)
    print('   baked %-18s %-9s res=%d mean=%.3f' % (o.name[:18], bake_type, res, mean))
    for m, n in nodes:
        m.node_tree.nodes.remove(n)
    return (img if mean > 0.005 else None), nodes


def bake_object(o, res, top_only=False, roughness=False):
    select_only(o)
    prepare_uv(o, top_only)
    col, _ = bake_pass(o, res, 'DIFFUSE', 'col')
    rough = None
    if roughness:
        rough, _ = bake_pass(o, res, 'ROUGHNESS', 'rough')
    if col is None:
        return
    for i, slot in enumerate(o.material_slots):
        m = slot.material
        if m is None:
            continue
        mm = m.copy()
        safe = ''.join(c if c.isalnum() or c in '._-' else '_' for c in o.name)
        mm.name = 'rb_%s_%d' % (safe, i)
        slot.material = mm
        nt = mm.node_tree
        b = next((x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED'), None)
        if not b:
            continue
        tc = nt.nodes.new('ShaderNodeTexImage'); tc.image = col
        for l in list(b.inputs['Base Color'].links):
            nt.links.remove(l)
        nt.links.new(tc.outputs['Color'], b.inputs['Base Color'])
        if rough is not None:
            tr = nt.nodes.new('ShaderNodeTexImage'); tr.image = rough
            for l in list(b.inputs['Roughness'].links):
                nt.links.remove(l)
            nt.links.new(tr.outputs['Color'], b.inputs['Roughness'])
        # the projected normal map is wrong on these UVs too; better none
        for l in list(b.inputs['Normal'].links):
            nt.links.remove(l)

    # The baked images are authored against `bake_uv`. The exporter writes UV
    # layers in order, so with the artist's own map still first the bake lands
    # in TEXCOORD_1 -- which the engine never reads -- and TEXCOORD_0 samples
    # the bake through a layout it was not made for. Drop the original: the
    # baked material is the only one this mesh has now.
    me = o.data
    for layer in list(me.uv_layers):
        if layer.name != 'bake_uv':
            me.uv_layers.remove(layer)
    me.uv_layers['bake_uv'].active = True
    me.uv_layers['bake_uv'].active_render = True


floor = bpy.data.objects.get('Plane.016')
if floor:
    bake_object(floor, 2048, top_only=True, roughness=True)

seen = set()
for o in bpy.data.objects:
    if o.type == 'MESH' and o.name.startswith('Cylinder.0') and \
            any(s.material and s.material.name.startswith('concrete') for s in o.material_slots):
        if o.data.name in seen:
            continue
        seen.add(o.data.name)
        bake_object(o, 1024)

gl = os.path.join(OUT, 'underground_garage_baked.gltf')
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.ops.export_scene.gltf(filepath=gl, export_format='GLTF_SEPARATE',
                          use_selection=True, export_yup=True,
                          export_materials='EXPORT', export_image_format='AUTO',
                          export_cameras=False, export_lights=False,
                          export_animations=False)
print('EXPORTED', gl)
