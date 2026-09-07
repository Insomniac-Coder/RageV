"""Unpack every packed image, and trace each material's graph to find which
image feeds which Principled input.

The FBX and OBJ exporters only carry a texture when an Image Texture node
connects *directly* to a Principled socket. This scene routes almost all of
its through Mapping nodes and node groups, which is why thirteen materials'
worth of texture arrived as four. Walking the graph backwards finds them.
"""
import bpy, os, json

OUT = r'C:\Users\ism19\Code\RageV\tools\scripts\garage\blendtex'
os.makedirs(OUT, exist_ok=True)

# --- unpack -----------------------------------------------------------------
saved = {}
for im in bpy.data.images:
    if im.source == 'VIEWER' or im.size[0] == 0:
        continue
    safe = ''.join(c if c.isalnum() or c in '._-' else '_' for c in im.name)
    if not safe.lower().endswith(('.png', '.jpg', '.jpeg')):
        safe += '.png'
    path = os.path.join(OUT, safe)
    try:
        im.filepath_raw = path
        im.file_format = 'PNG' if safe.lower().endswith('.png') else 'JPEG'
        im.save()
        saved[im.name] = safe
    except Exception as e:
        print('  could not save', im.name, e)
print('unpacked %d images to %s' % (len(saved), OUT))

# --- trace ------------------------------------------------------------------
SLOTS = {'Base Color': 'BaseColor', 'Roughness': 'Roughness',
         'Metallic': 'Metallic', 'Normal': 'Normal', 'Alpha': 'Alpha',
         'Emission Color': 'Emissive'}


def first_image(socket, depth=0, seen=None):
    """Walk backwards from a socket to the first Image Texture we can reach."""
    if seen is None:
        seen = set()
    if depth > 12 or not socket.is_linked:
        return None, None
    link = socket.links[0]
    node = link.from_node
    if node in seen:
        return None, None
    seen.add(node)
    if node.type == 'TEX_IMAGE':
        scale = None
        # a Mapping node feeding this texture's vector gives us the tiling
        if 'Vector' in node.inputs and node.inputs['Vector'].is_linked:
            up = node.inputs['Vector'].links[0].from_node
            if up.type == 'MAPPING' and 'Scale' in up.inputs:
                scale = tuple(round(v, 4) for v in up.inputs['Scale'].default_value)
        return (node.image.name if node.image else None), scale
    if node.type == 'GROUP' and node.node_tree:
        # step inside: find the group output, follow what feeds it
        for gn in node.node_tree.nodes:
            if gn.type == 'GROUP_OUTPUT':
                for gi in gn.inputs:
                    r = first_image(gi, depth + 1, seen)
                    if r[0]:
                        return r
    for inp in node.inputs:
        r = first_image(inp, depth + 1, seen)
        if r[0]:
            return r
    return None, None


result = {}
for m in bpy.data.materials:
    if not m.use_nodes or not m.node_tree:
        continue
    bsdf = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if not bsdf:
        continue
    entry = {}
    for sock, name in SLOTS.items():
        if sock not in bsdf.inputs:
            continue
        s = bsdf.inputs[sock]
        if s.is_linked:
            img, scale = first_image(s)
            if img and img in saved:
                entry[name] = {'file': saved[img]}
                if scale:
                    entry[name]['scale'] = scale
        else:
            v = s.default_value
            try:
                entry[name + 'Value'] = [round(c, 4) for c in v]
            except TypeError:
                entry[name + 'Value'] = round(v, 4)
    if entry:
        result[m.name] = entry

with open(os.path.join(OUT, 'materials.json'), 'w') as f:
    json.dump(result, f, indent=1)
print('traced %d materials' % len(result))
for name in sorted(result):
    got = [k for k in result[name] if not k.endswith('Value')]
    print('  %-34s %s' % (name[:34], ', '.join(got) or '(values only)'))
