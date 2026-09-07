"""A complete PBR set from the .blend, and a fresh export to garage_pbr/.

Run:  blender --background --factory-startup <blend> --python-exit-code 1
             --python tools/scripts/garage/bake_pbr.py

What it does, in order, and why each step is where it is:

1. Applies every modifier. The arrays and geometry nodes that make the ceiling
   grid, the beam rows and the hanger rows are modifiers, and the glTF exporter
   does not apply them unless told; every earlier export carried one slab, one
   beam and one hanger where Cycles draws rows of them. Baking also needs the
   real geometry: arrayed copies share the base mesh's UVs, so a bake through
   them writes every copy onto the same texels.
2. Turns the room shell inward. Cycles draws both sides; the engine culls the
   back, so without this the walls and ceiling are simply absent.
3. Decides, per object, whether it needs baking at all. A Principled material
   whose linked inputs are plain Image Textures on the UV map is carried by
   the exporter as it stands (the ladder, the tyre pile, the jerry cans) and
   is left alone. Anything routed through a Mapping node (tiling the importer
   does not read), a node group, a Bump, a colour ramp or object-space
   coordinates (the concrete, the floor) is baked. A material with a linked
   Alpha -- the graffiti decals -- is never baked: the cut-out would be lost.
4. Gives each baked mesh a `bake_uv` layer (smart projection; the floor is a
   cube projection with everything but its top pushed off the page, so the
   whole page is the surface you can see). `bake_uv` is the *active* layer,
   the bake target; the artist's layer stays *render-active* so the materials
   that read it keep working while they are baked. It is removed only after
   every bake, because the engine reads TEXCOORD_0 and only that.
5. Bakes what each object's materials actually link: DIFFUSE colour-only
   (with Metallic forced to zero for the pass, or a metal's albedo bakes
   black), ROUGHNESS, tangent-space NORMAL (+Y, the glTF convention), and
   Metallic through an EMIT pass, since Blender has no metallic bake -- the
   metallic source is wired to Emission for the duration and put back.
   Resolution follows surface area. Linked duplicates are baked once.
6. Rewires a private copy of each Principled material to the baked images,
   drops the artist's UV layers, and exports GLTF_SEPARATE (GLB crashes on
   this file). Emission strengths travel in KHR_materials_emissive_strength,
   which the importer now reads.

A manifest of what was baked, at what size, with each image's mean, is
written beside the export as bake_manifest.json.
"""
import bpy, os, bmesh, json, math, time
import numpy as np
from mathutils import Vector

OUT = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage_pbr'
FLOOR = 'Plane.016'
SHELL = 'Cube'
CHANNELS = (('Base Color', 'col'), ('Roughness', 'rough'), ('Metallic', 'metal'), ('Normal', 'normal'))
T0 = time.time()


def log(*a):
    print('[bake %5.0fs]' % (time.time() - T0), *a, flush=True)


import shutil
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    path = os.path.join(OUT, f)
    # the FBX exporter's texture folder (<name>.fbm) is a directory
    shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)

# --- Cycles, on the GPU ------------------------------------------------------
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
    log('gpu setup failed, CPU it is:', e)
sc.cycles.samples = 8
bs = sc.render.bake
bs.use_pass_direct = False
bs.use_pass_indirect = False
bs.use_pass_color = True
bs.use_selected_to_active = False
bs.use_clear = True
bs.normal_space = 'TANGENT'
bs.normal_r, bs.normal_g, bs.normal_b = 'POS_X', 'POS_Y', 'POS_Z'


def select_only(o):
    bpy.ops.object.select_all(action='DESELECT')
    o.hide_set(False)
    o.hide_viewport = False
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


def safe_name(s):
    return ''.join(c if c.isalnum() or c in '._-' else '_' for c in s)


# --- 1. modifiers, applied ---------------------------------------------------
with_mods = [o for o in bpy.data.objects if o.type == 'MESH' and o.modifiers]
if with_mods:
    bpy.ops.object.select_all(action='DESELECT')
    for o in with_mods:
        o.hide_set(False)
        o.hide_viewport = False
        o.select_set(True)
    bpy.context.view_layer.objects.active = with_mods[0]
    bpy.ops.object.convert(target='MESH')
    log('applied modifiers on %d objects: %s' % (len(with_mods), ', '.join(o.name for o in with_mods)))
    for o in with_mods:
        assert not o.modifiers, '%s still carries modifiers' % o.name

# --- 1b. solids face outward ---------------------------------------------------
# The floor came out of its geometry nodes as a 1.9 m slab whose top faces
# point down and whose bottom faces point up. Cycles draws both sides and
# never noticed; the engine culled the real top, showed the underside two
# metres lower, and every mirror ray from it hit the true top from behind
# and came back as the floor's own dark texel -- which is why no test of the
# reflections ever put a tube in the floor. A closed mesh with outward
# normals has positive signed volume; any converted mesh below zero is
# inside out and is flipped whole. Open meshes (the decals, the shell) have
# no meaningful sign and are left alone.
flipped = []
for o in bpy.data.objects:
    if o.type != 'MESH' or o.name == SHELL or len(o.data.polygons) < 4:
        continue
    bm = bmesh.new()
    bm.from_mesh(o.data)
    volume = bm.calc_volume(signed=True)
    bm.free()
    if volume < -1e-6:
        select_only(o)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.flip_normals()
        bpy.ops.object.mode_set(mode='OBJECT')
        flipped.append('%s (%.1f m3)' % (o.name, volume))
log('flipped %d inside-out meshes: %s' % (len(flipped), ', '.join(flipped)))

# --- 2. the shell faces the room --------------------------------------------
shell = bpy.data.objects.get(SHELL)
if shell:
    select_only(shell)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    log('shell normals face inward')


# --- 3. what needs baking ----------------------------------------------------
def principled(m):
    if m is None or not m.node_tree:
        return None
    outs = [n for n in m.node_tree.nodes if n.type == 'OUTPUT_MATERIAL']
    out = next((n for n in outs if n.is_active_output), outs[0] if outs else None)
    if out is None or not out.inputs['Surface'].is_linked:
        return None
    n = out.inputs['Surface'].links[0].from_node
    return n if n.type == 'BSDF_PRINCIPLED' else None


def image_is_plain(node):
    """An Image Texture the exporter carries as it stands: nothing on its
    Vector, or the UV map itself. A Mapping node means tiling the importer
    does not read; Object coordinates mean a layout no UV set has."""
    v = node.inputs.get('Vector')
    if v is None or not v.is_linked:
        return True
    up = v.links[0].from_node
    if up.type == 'UVMAP':
        return True
    return up.type == 'TEX_COORD' and v.links[0].from_socket.name == 'UV'


def channel_clean(sock, key):
    if not sock.is_linked:
        return True
    n = sock.links[0].from_node
    if key == 'Normal':
        if n.type != 'NORMAL_MAP':
            return False
        c = n.inputs['Color']
        if not c.is_linked:
            return True
        n = c.links[0].from_node
    return n.type == 'TEX_IMAGE' and image_is_plain(n)


def decal_alpha(m):
    """Where a material's transparency comes from, or None if it has none.

    Two shapes in this file: a Principled with something wired into Alpha
    (scribble2, scribble3), and a Mix Shader between a Transparent BSDF and a
    Principled whose factor is the cut-out (scribbles, scribble1). The
    exporter derives an alpha for both, but from the image's own channel, not
    from the graph that turns the black of the artwork into the hole -- so
    the decals arrived as mostly-opaque cards. Baking the graph's own value
    through an emission pass is exact.

    Returns (kind, socket, invert, principled_node): `socket` is the input
    socket whose linked source (or default value) is the alpha; `invert` says
    a mix factor selects the transparent side, so alpha is one minus it.

    The mix in this file turned out to be a Light Path switch (camera rays
    see the Principled, everything else the Transparent, so the decal casts
    no shadow) and the cut-out lives on the Principled's own Alpha behind it
    -- so when the shader on the other side of the mix is a Principled with a
    linked Alpha, that is the alpha, and the factor is not.
    """
    if m is None or not m.node_tree:
        return None
    b = principled(m)
    if b is not None:
        a = b.inputs['Alpha']
        return ('principled', a, False, b) if a.is_linked else None
    outs = [n for n in m.node_tree.nodes if n.type == 'OUTPUT_MATERIAL']
    out = next((n for n in outs if n.is_active_output), outs[0] if outs else None)
    if out is None or not out.inputs['Surface'].is_linked:
        return None
    mix = out.inputs['Surface'].links[0].from_node
    if mix.type != 'MIX_SHADER':
        return None
    shaders = [s.links[0].from_node if s.is_linked else None for s in mix.inputs[1:3]]
    kinds = [s.type if s else None for s in shaders]
    if 'BSDF_TRANSPARENT' not in kinds:
        return None
    other = next((s for s in shaders if s is not None and s.type != 'BSDF_TRANSPARENT'), None)
    if other is not None and other.type == 'BSDF_PRINCIPLED' and other.inputs['Alpha'].is_linked:
        return ('principled', other.inputs['Alpha'], False, other)
    # fac = 0 gives the first shader, fac = 1 the second
    return ('mix', mix.inputs['Fac'], kinds[1] == 'BSDF_TRANSPARENT', other)


def bake_plan(o):
    """None if the exporter can carry this object as it is; else the set of
    channel tags to bake. A decal (any slot with transparency) bakes colour
    and alpha only -- it is a flat sheet, whatever else its graph links."""
    if any(decal_alpha(s.material) for s in o.material_slots):
        return {'col', 'alpha'}
    needed = False
    tags = set()
    for slot in o.material_slots:
        b = principled(slot.material)
        if b is None:
            continue
        for key, tag in CHANNELS:
            if b.inputs[key].is_linked:
                tags.add(tag)
                if not channel_clean(b.inputs[key], key):
                    needed = True
    return tags if needed and tags else None


def area_of(o):
    return sum(p.area for p in o.data.polygons) * abs(o.scale.x * o.scale.y * o.scale.z) ** (2 / 3.0)


def resolution_for(area):
    return 4096 if area >= 1500 else 2048 if area >= 100 else 1024 if area >= 15 else 512 if area >= 2 else 256


# --- 4. UVs ------------------------------------------------------------------
def prepare_uv(o):
    me = o.data
    if 'bake_uv' in me.uv_layers:
        me.uv_layers.remove(me.uv_layers['bake_uv'])
    artist = [l for l in me.uv_layers if l.active_render]
    layer = me.uv_layers.new(name='bake_uv')
    me.uv_layers.active = layer                 # the bake writes here...
    if artist:
        artist[0].active_render = True          # ...the materials keep reading there
        layer.active_render = False
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    if o.name == FLOOR:
        bpy.ops.uv.cube_project(cube_size=1.0, correct_aspect=True, scale_to_bounds=True)
    else:
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.004,
                                 scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    if o.name == FLOOR:
        bm = bmesh.new()
        bm.from_mesh(me)
        uv = bm.loops.layers.uv['bake_uv']
        up = Vector((0.0, 0.0, 1.0))
        for f in bm.faces:
            if f.normal.dot(up) < 0.5:
                for l in f.loops:
                    l[uv].uv = l[uv].uv + Vector((10.0, 10.0))
        bm.to_mesh(me)
        bm.free()
    assert me.uv_layers.active.name == 'bake_uv'


# --- 5. the passes -----------------------------------------------------------
def temp_image_nodes(o, img):
    nodes = []
    for slot in o.material_slots:
        m = slot.material
        if m is None or not m.node_tree:
            continue
        n = m.node_tree.nodes.new('ShaderNodeTexImage')
        n.image = img
        n.select = True
        m.node_tree.nodes.active = n
        nodes.append((m, n))
    return nodes


def prep_col(o):
    """Metallic to zero for the albedo pass: Cycles' diffuse colour of a metal
    is black, and the shell, the pipes and the fittings are all metals."""
    undo = []
    for slot in o.material_slots:
        b = principled(slot.material)
        if b is None:
            continue
        s = b.inputs['Metallic']
        nt = slot.material.node_tree
        link = s.links[0] if s.is_linked else None
        src = link.from_socket if link else None
        val = s.default_value
        if link:
            nt.links.remove(link)
        s.default_value = 0.0
        undo.append((nt, s, src, val))

    def restore():
        for nt, s, src, val in undo:
            s.default_value = val
            if src is not None:
                nt.links.new(src, s)
    return restore


def prep_metal(o):
    """Metallic through the emission pass: whatever feeds Metallic feeds
    Emission Color at strength one for the duration, then goes back."""
    undo = []
    for slot in o.material_slots:
        b = principled(slot.material)
        if b is None:
            continue
        nt = slot.material.node_tree
        ec = b.inputs['Emission Color']
        es = b.inputs['Emission Strength']
        ec_link = ec.links[0].from_socket if ec.is_linked else None
        ec_val = tuple(ec.default_value)
        es_link = es.links[0].from_socket if es.is_linked else None
        es_val = es.default_value
        for l in list(ec.links) + list(es.links):
            nt.links.remove(l)
        m = b.inputs['Metallic']
        if m.is_linked:
            nt.links.new(m.links[0].from_socket, ec)
        else:
            v = m.default_value
            ec.default_value = (v, v, v, 1.0)
        es.default_value = 1.0
        undo.append((nt, ec, es, ec_link, ec_val, es_link, es_val))

    def restore():
        for nt, ec, es, ec_link, ec_val, es_link, es_val in undo:
            for l in list(ec.links):
                nt.links.remove(l)
            ec.default_value = ec_val
            es.default_value = es_val
            if ec_link is not None:
                nt.links.new(ec_link, ec)
            if es_link is not None:
                nt.links.new(es_link, es)
    return restore


def prep_alpha(o):
    """The graph's own alpha through the emission pass. A Principled decal
    gets its alpha source wired to Emission Color at strength one, with Alpha
    itself set to one for the duration -- Principled scales its whole closure
    by Alpha, emission included, which would bake alpha squared. A mix-shader
    decal gets a temporary Emission shader on the output, fed by the mix
    factor (or one minus it)."""
    undo = []
    for slot in o.material_slots:
        m = slot.material
        info = decal_alpha(m)
        if info is None:
            continue
        kind, sock, invert, b = info
        nt = m.node_tree
        if kind == 'principled':
            ec, es, al = b.inputs['Emission Color'], b.inputs['Emission Strength'], b.inputs['Alpha']
            saved = (ec.links[0].from_socket if ec.is_linked else None, tuple(ec.default_value),
                     es.links[0].from_socket if es.is_linked else None, es.default_value,
                     al.links[0].from_socket, al.default_value)
            for l in list(ec.links) + list(es.links) + list(al.links):
                nt.links.remove(l)
            nt.links.new(saved[4], ec)
            es.default_value = 1.0
            al.default_value = 1.0

            def restore(nt=nt, ec=ec, es=es, al=al, saved=saved):
                for l in list(ec.links):
                    nt.links.remove(l)
                ec.default_value = saved[1]
                es.default_value = saved[3]
                al.default_value = saved[5]
                if saved[0] is not None:
                    nt.links.new(saved[0], ec)
                if saved[2] is not None:
                    nt.links.new(saved[2], es)
                nt.links.new(saved[4], al)
            undo.append(restore)
        else:
            outs = [n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL']
            out = next((n for n in outs if n.is_active_output), outs[0])
            surf = out.inputs['Surface']
            original = surf.links[0].from_socket
            emit = nt.nodes.new('ShaderNodeEmission')
            emit.inputs['Strength'].default_value = 1.0
            temp = [emit]
            src = sock.links[0].from_socket if sock.is_linked else None
            if invert:
                sub = nt.nodes.new('ShaderNodeMath')
                sub.operation = 'SUBTRACT'
                sub.inputs[0].default_value = 1.0
                if src is not None:
                    nt.links.new(src, sub.inputs[1])
                else:
                    sub.inputs[1].default_value = sock.default_value
                nt.links.new(sub.outputs[0], emit.inputs['Color'])
                temp.append(sub)
            elif src is not None:
                nt.links.new(src, emit.inputs['Color'])
            else:
                v = sock.default_value
                emit.inputs['Color'].default_value = (v, v, v, 1.0)
            for l in list(surf.links):
                nt.links.remove(l)
            nt.links.new(emit.outputs['Emission'], surf)

            def restore(nt=nt, surf=surf, original=original, temp=temp):
                for l in list(surf.links):
                    nt.links.remove(l)
                nt.links.new(original, surf)
                for n in temp:
                    nt.nodes.remove(n)
            undo.append(restore)

    def restore_all():
        for r in undo:
            r()
    return restore_all


PASS = {'col': ('DIFFUSE', prep_col), 'rough': ('ROUGHNESS', None),
        'normal': ('NORMAL', None), 'metal': ('EMIT', prep_metal),
        'alpha': ('EMIT', prep_alpha)}


def compose_rgba(o, col, alpha, res):
    """One RGBA sheet from the colour and alpha bakes: what a cut-out
    material wants to read, and what the exporter writes as-is."""
    safe = safe_name(o.name)
    rgba = bpy.data.images.new('pbr_%s_rgba' % safe, res, res, alpha=True)
    rgba.colorspace_settings.name = 'sRGB'
    c = np.empty(res * res * 4, dtype=np.float32)
    a = np.empty(res * res * 4, dtype=np.float32)
    col.pixels.foreach_get(c)
    alpha.pixels.foreach_get(a)
    c[3::4] = a[0::4]
    rgba.pixels.foreach_set(c)
    path = os.path.join(OUT, 'pbr_%s_rgba.png' % safe)
    rgba.filepath_raw = path
    rgba.file_format = 'PNG'
    rgba.save()
    for im in (col, alpha):
        try:
            os.remove(im.filepath_raw)
        except OSError:
            pass
        bpy.data.images.remove(im)
    return rgba


def bake_pass(o, res, tag):
    kind, prep = PASS[tag]
    safe = safe_name(o.name)
    img = bpy.data.images.new('pbr_%s_%s' % (safe, tag), res, res, alpha=False)
    img.colorspace_settings.name = 'sRGB' if tag == 'col' else 'Non-Color'
    nodes = temp_image_nodes(o, img)
    restore = prep(o) if prep else None
    bs.margin = max(8, res // 128)
    try:
        bpy.ops.object.bake(type=kind)
    finally:
        if restore:
            restore()
        for m, n in nodes:
            m.node_tree.nodes.remove(n)
    path = os.path.join(OUT, 'pbr_%s_%s.png' % (safe, tag))
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    buf = np.empty(len(img.pixels), dtype=np.float32)
    img.pixels.foreach_get(buf)
    mean = float(buf[0::4].mean())
    return img, mean


# --- 6. rewiring ---------------------------------------------------------------
def rewire(o, images):
    safe = safe_name(o.name)
    for i, slot in enumerate(o.material_slots):
        m = slot.material
        if principled(m) is None:
            continue
        mm = m.copy()
        mm.name = 'pbr_%s_%d' % (safe, i)
        slot.material = mm
        nt = mm.node_tree
        b = principled(mm)
        for key, tag in CHANNELS:
            s = b.inputs[key]
            if not s.is_linked or tag not in images:
                continue
            for l in list(s.links):
                nt.links.remove(l)
            tex = nt.nodes.new('ShaderNodeTexImage')
            tex.image = images[tag]
            if tag == 'col':
                nt.links.new(tex.outputs['Color'], s)
            elif tag == 'normal':
                nm = nt.nodes.new('ShaderNodeNormalMap')
                nm.space = 'TANGENT'
                nt.links.new(tex.outputs['Color'], nm.inputs['Color'])
                nt.links.new(nm.outputs['Normal'], s)
            else:
                sep = nt.nodes.new('ShaderNodeSeparateColor')
                nt.links.new(tex.outputs['Color'], sep.inputs['Color'])
                nt.links.new(sep.outputs['Red'], s)
    drop_artist_uvs(o)


def drop_artist_uvs(o):
    me = o.data
    for layer in list(me.uv_layers):
        if layer.name != 'bake_uv':
            me.uv_layers.remove(layer)
    me.uv_layers['bake_uv'].active = True
    me.uv_layers['bake_uv'].active_render = True


def rewire_decal(o, rgba):
    """A fresh cut-out material per slot: the RGBA sheet into Base Color and,
    through a `> 0.5` test, into Alpha. The test is what the exporter reads
    as alphaMode MASK (search_node_tree.detect_alpha_clip), and Masked is
    the mode the engine already cut the old graffiti with."""
    safe = safe_name(o.name)
    for i, slot in enumerate(o.material_slots):
        info = decal_alpha(slot.material)
        if info is None:
            continue
        src = info[3]
        rough = src.inputs['Roughness'].default_value if src else 0.7
        mm = bpy.data.materials.new('pbr_%s_%d' % (safe, i))
        mm.use_nodes = True
        nt = mm.node_tree
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        b.inputs['Roughness'].default_value = rough
        b.inputs['Metallic'].default_value = 0.0
        tex = nt.nodes.new('ShaderNodeTexImage')
        tex.image = rgba
        cut = nt.nodes.new('ShaderNodeMath')
        cut.operation = 'GREATER_THAN'
        cut.inputs[1].default_value = 0.5
        nt.links.new(tex.outputs['Color'], b.inputs['Base Color'])
        nt.links.new(tex.outputs['Alpha'], cut.inputs[0])
        nt.links.new(cut.outputs[0], b.inputs['Alpha'])
        nt.links.new(b.outputs['BSDF'], out.inputs['Surface'])
        for attr, val in (('blend_method', 'CLIP'), ('alpha_threshold', 0.5),
                          ('surface_render_method', 'DITHERED')):
            try:
                setattr(mm, attr, val)
            except Exception:
                pass
        mm.use_backface_culling = False
        slot.material = mm
    drop_artist_uvs(o)


# --- run -----------------------------------------------------------------------
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and len(o.material_slots)]
meshes.sort(key=area_of, reverse=True)
manifest = {}
seen = {}
baked_images = {}
for o in meshes:
    plan = bake_plan(o)
    if plan is None:
        manifest[o.name] = {'baked': False}
        continue
    if o.data.name in seen:
        manifest[o.name] = {'baked': False, 'shares': seen[o.data.name]}
        continue
    seen[o.data.name] = o.name
    area = area_of(o)
    res = resolution_for(area)
    if 'alpha' in plan:
        res = max(res, 1024)      # a decal is the wall behind the car in every shot
    select_only(o)
    prepare_uv(o)
    images, means = {}, {}
    for tag in ('col', 'rough', 'metal', 'normal', 'alpha'):
        if tag not in plan:
            continue
        img, mean = bake_pass(o, res, tag)
        images[tag] = img
        means[tag] = round(mean, 4)
    if 'alpha' in images:
        images = {'rgba': compose_rgba(o, images['col'], images['alpha'], res)}
    baked_images[o.name] = images
    manifest[o.name] = {'baked': True, 'res': res, 'area': round(area, 1), 'means': means,
                        'materials': [s.material.name if s.material else None for s in o.material_slots]}
    log('%-26s res=%-4d area=%-7.1f %s' % (o.name[:26], res, area,
                                            ' '.join('%s=%.3f' % kv for kv in means.items())))

for name, images in baked_images.items():
    if 'rgba' in images:
        rewire_decal(bpy.data.objects[name], images['rgba'])
    else:
        rewire(bpy.data.objects[name], images)
log('rewired %d objects' % len(baked_images))

with open(os.path.join(OUT, 'bake_manifest.json'), 'w') as f:
    json.dump(manifest, f, indent=1, sort_keys=True)

# --- export --------------------------------------------------------------------
hidden = [o.name for o in bpy.data.objects if o.type == 'MESH' and o.hide_render]
if hidden:
    log('NOTE: hidden from render, exported anyway:', hidden)
bpy.ops.object.select_all(action='DESELECT')
for o in bpy.data.objects:
    if o.type == 'MESH':
        o.hide_set(False)
        o.hide_viewport = False
        o.select_set(True)
gl = os.path.join(OUT, 'underground_garage_pbr.gltf')
bpy.ops.export_scene.gltf(filepath=gl, export_format='GLTF_SEPARATE',
                          use_selection=True, export_yup=True, export_apply=True,
                          export_materials='EXPORT', export_image_format='AUTO',
                          export_cameras=False, export_lights=False,
                          export_animations=False)
log('EXPORTED', gl, '%d baked objects, %d files' % (len(baked_images), len(os.listdir(OUT))))

# The same model as FBX, beside the glTF. The FBX exporter copies each
# image from the path it has on disk, and the artist's images are packed
# into the .blend with paths that exist on nobody's machine -- so they are
# written out first, under their own names, where the glTF exporter already
# put its copies. The baked images already live in OUT.
used = set()
for m in bpy.data.materials:
    if m.node_tree:
        for n in m.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                used.add(n.image)
unpacked = 0
for im in used:
    if im.packed_file is None:
        continue
    safe = safe_name(im.name)
    if not safe.lower().endswith(('.png', '.jpg', '.jpeg')):
        safe += '.png'
    im.filepath_raw = os.path.join(OUT, safe)
    im.file_format = 'PNG' if safe.lower().endswith('.png') else 'JPEG'
    im.save()
    unpacked += 1
log('wrote %d packed images beside the model' % unpacked)
# Outside the assets tree on purpose: the runtime imports every model it
# finds under assets/, and an FBX beside the glTF produced materials with
# the glTF's names and overwrote them mid-run (2026-09-05).
FBX_DIR = r'C:\Users\ism19\Code\RageV\build\garage_export'
os.makedirs(FBX_DIR, exist_ok=True)
for f in os.listdir(FBX_DIR):
    path = os.path.join(FBX_DIR, f)
    shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
fb = os.path.join(FBX_DIR, 'underground_garage_pbr.fbx')
bpy.ops.export_scene.fbx(filepath=fb, use_selection=True, object_types={'MESH'},
                         apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
                         use_mesh_modifiers=True, mesh_smooth_type='OFF',
                         path_mode='COPY', embed_textures=False,
                         add_leaf_bones=False, bake_anim=False,
                         axis_forward='-Z', axis_up='Y')
log('EXPORTED', fb, '%.1f MB' % (os.path.getsize(fb) / 1e6))

# The baked state itself, so another export -- a different format, a
# different setting -- is a one-minute job rather than a re-bake.
saved = os.path.join(r'C:\Users\ism19\Code\RageV\build', 'garage_pbr_baked.blend')
bpy.ops.wm.save_as_mainfile(filepath=saved, copy=True)
log('SAVED', saved)
