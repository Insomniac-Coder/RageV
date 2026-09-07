"""Put back the one thing glTF cannot carry: emissive strength.

glTF's emissiveFactor is 0..1, so Blender's Emission Strength -- 16 on the
strip tubes, 12 on the second set, 100 on the ceiling panel -- is dropped on
export and every emitter arrives at 1.0. The colours are exact; only the
magnitude is missing, so it is multiplied back in here.

(KHR_materials_emissive_strength exists for this, and neither the exporter's
default settings nor our importer speak it.)
"""
import io, os, re, glob

D = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage_gltf'

STRENGTH = {'light': 16.0, 'light2': 12.0, 'Material.001': 100.0,
            'Blinking Light': 8.0}


def path_of(name):
    for p in glob.glob(os.path.join(D, '*.rmat')):
        m = re.match(r'^underground_garage_\d+_(.*)\.rmat$', os.path.basename(p))
        if m and m.group(1) == name:
            return p
    return None


for name, k in STRENGTH.items():
    p = path_of(name)
    if not p:
        print('  no material', name)
        continue
    raw = io.open(p, 'r', encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    out = []
    for ln in raw.replace('\r\n', '\n').split('\n'):
        m = re.match(r'^Emissive: \[([-\d.eE]+), ([-\d.eE]+), ([-\d.eE]+), ([-\d.eE]+)\]$', ln)
        if m:
            r, g, b = (float(m.group(i)) * k for i in (1, 2, 3))
            out.append('Emissive: [%.4g, %.4g, %.4g, 1]' % (r, g, b))
        elif ln.startswith('Metallic:'):
            out.append('Metallic: 0')     # an emitter is not a metal
        else:
            out.append(ln)
    while out and not out[-1].strip():
        out.pop()
    io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(out) + nl)
    print('  %-16s x%g' % (name, k))
