"""Wire the textures recovered from the .blend into the garage's materials.

The FBX and OBJ exporters carried four materials' textures; the .blend has
thirteen. The difference is not that the others were procedural -- they route
through Mapping and node-group nodes, and those two exporters only follow an
Image Texture wired straight into a Principled socket.

Tiling comes from each material's Mapping node scale, so the repeat is the one
the artist set rather than one guessed from the room's size.

Line-based on purpose: these files are CRLF with no trailing newline, and a
multiline regex both eats the carriage return and appends onto the last line.
"""
import io, os, re, glob

D = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage'
B = os.path.join(D, 'blend')

handles = {}
for m in glob.glob(os.path.join(B, '*.meta')):
    h = re.search(r'Handle: (\d+)', io.open(m, 'r', encoding='utf-8').read()).group(1)
    handles[os.path.basename(m)[:-5].lower()] = h

CONCRETE = 'TexturesCom_ConcreteBare0451_2_seamless_S.jpg'
FLOOR = 'ConcreteFloorsDamaged0041_1_download600.jpg.002.png'
SCRATCH = 'scratched-plastic-texture-9.jpg.001.png'

WIRE = {
    'concrete': (dict(BaseColor='[0.62, 0.63, 0.66, 1]', Roughness='0.92',
                      Metallic='0'), {'BaseColor': CONCRETE}, '[0.52, 0.52]'),
    'concrete.006': (dict(BaseColor='[0.58, 0.6, 0.63, 1]', Roughness='0.93',
                          Metallic='0'), {'BaseColor': CONCRETE}, '[0.27, 0.27]'),
    'Material.014': (dict(BaseColor='[0.5, 0.52, 0.56, 1]', Roughness='0.9',
                          Metallic='0'), {'BaseColor': CONCRETE}, '[0.4, 0.4]'),
    'wet road.001': (dict(BaseColor='[0.33, 0.34, 0.36, 1]', Roughness='0.22',
                          Metallic='0', Specular='0.7'),
                     {'BaseColor': FLOOR}, '[0.15, 0.15]'),
    'metalic shader.001': (dict(BaseColor='[0.55, 0.56, 0.6, 1]', Roughness='0.4',
                                Metallic='0.9'), {'BaseColor': SCRATCH}, '[1, 1]'),
}
# Colour and alpha from one sheet: the art is painted on black and the black is
# the cut-out, which is why these read as solid panels without it.
GRAF = 'graffitistuff.png.001.png'
for mat, tex in (('scribble1.001', GRAF), ('scribble2.001', GRAF),
                 ('scribble3.001', GRAF), ('scribbles', GRAF)):
    WIRE[mat] = (dict(BaseColor='[1, 1, 1, 1]', Roughness='0.95', Metallic='0',
                      Blend='Masked'), {'BaseColor': tex}, '[1, 1]')


def path_of(name):
    for p in glob.glob(os.path.join(D, '*.rmat')):
        m = re.match(r'^underground_garage_\d+_(.*)\.rmat$', os.path.basename(p))
        if m and m.group(1) == name:
            return p
    return None


done = 0
for name, (fields, maps, tiling) in WIRE.items():
    p = path_of(name)
    if not p:
        print('  no material file for', name)
        continue
    raw = io.open(p, 'r', encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.replace('\r\n', '\n').split('\n')

    head, existing = [], {}
    in_maps = False
    for ln in lines:
        if ln.strip() == 'Maps:':
            in_maps = True
            continue
        if in_maps:
            g = re.match(r'\s+(\w+):\s*(\d+)\s*$', ln)
            if g:
                existing[g.group(1)] = g.group(2)
                continue
            if not ln.strip():
                continue
            in_maps = False
        head.append(ln)
    while head and not head[-1].strip():
        head.pop()

    fields = dict(fields, Tiling=tiling)
    seen = set()
    out = []
    for ln in head:
        k = ln.split(':', 1)[0] if ':' in ln else None
        if k in fields:
            out.append('%s: %s' % (k, fields[k]))
            seen.add(k)
        else:
            out.append(ln)
    for k, v in fields.items():                 # Blend is absent on opaques
        if k not in seen:
            out.insert(len(out), '%s: %s' % (k, v))

    for slot, tex in maps.items():
        if tex.lower() in handles:
            existing[slot] = handles[tex.lower()]
        else:
            print('  MISSING HANDLE for', tex)
    if existing:
        out.append('Maps:')
        for k, v in existing.items():
            out.append('  %s: %s' % (k, v))

    io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(out) + nl)
    done += 1
    print('  %-22s -> %s' % (name, ', '.join(maps.values())))
print('wired %d materials' % done)
