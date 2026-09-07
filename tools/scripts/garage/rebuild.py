"""Import the baked garage, rebuild showroom.rage, and measure it.

One command, so every iteration is the same iteration: rvimport from its own
folder, the studio scene migrated, Static flags, emissive strengths glTF cannot
carry, the shell as diffuse concrete, the artist's field of view, and the
render at the ground truth's framing with the numbers.
"""
import subprocess, os, sys, re, io, glob, shutil

SP = r'C:\Users\ism19\Code\RageV\tools\scripts\garage'
R = r'C:\Users\ism19\Code\RageV'
G = os.path.join(R, 'SampleProject', 'assets', 'models', 'garage_baked')
SCENE = os.path.join(R, 'SampleProject', 'assets', 'scenes', 'showroom.rage')
sys.path.insert(0, SP)


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr


# 1. fresh materials from the export
for f in glob.glob(os.path.join(G, '*.rmat')) + glob.glob(os.path.join(G, '*.rmat.meta')):
    os.remove(f)
out = run([os.path.join(R, 'build', 'bin', 'Release', 'rvimport', 'rvimport.exe'),
           'models/garage_baked/underground_garage_baked.gltf',
           '--project=' + os.path.join(R, 'SampleProject'), '--rhi=vulkan',
           '--out=' + os.path.join(R, 'tools', 'scripts', 'data', 'garage_baked.yaml')],
          cwd=os.path.join(R, 'build', 'bin', 'Release', 'rvimport'))
print('import: %d materials, errors=%d' % (len(glob.glob(os.path.join(G, '*.rmat'))),
                                          out.lower().count('error')))

# 2. the scene
shutil.copy(os.path.join(SP, 'showroom.rage.studio.bak'), SCENE)
print(run([sys.executable, os.path.join(SP, 'migrate.py')]).strip().splitlines()[-1])
print(run([sys.executable, os.path.join(SP, 'fix1.py')]).strip().splitlines()[-1])


def patch_rmat(name_match, fields):
    for p in glob.glob(os.path.join(G, '*.rmat')):
        m = re.match(r'^underground_garage_baked_\d+_(.*)\.rmat$', os.path.basename(p))
        if not m or not name_match(m.group(1)):
            continue
        raw = io.open(p, 'r', encoding='utf-8', newline='').read()
        nl = '\r\n' if '\r\n' in raw else '\n'
        outl = []
        for ln in raw.replace('\r\n', '\n').split('\n'):
            key = ln.split(':', 1)[0] if ':' in ln else None
            if key in fields:
                v = fields[key]
                outl.append('%s: %s' % (key, v(ln) if callable(v) else v))
            else:
                outl.append(ln)
        while outl and not outl[-1].strip():
            outl.pop()
        io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(outl) + nl)


# 3. emissive strength, which glTF's 0..1 factor drops
def scaled(k):
    def f(ln):
        g = re.match(r'^Emissive: \[([-\d.eE]+), ([-\d.eE]+), ([-\d.eE]+), ([-\d.eE]+)\]$', ln)
        return '[%.4g, %.4g, %.4g, 1]' % tuple(float(g.group(i)) * k for i in (1, 2, 3)) \
            if g else ln.split(':', 1)[1].strip()
    return f
for name, k in (('light', 16.0), ('light2', 12.0), ('Material.001', 100.0), ('Blinking Light', 8.0)):
    patch_rmat(lambda n, name=name: n == name, {'Emissive': scaled(k), 'Metallic': '0'})
print('emissive strengths restored')

# 4. the shell: the file says Metallic 1, which in Cycles is a blurred mirror of
#    a room full of bright tubes and reads as grey. Our environment is not lit
#    yet, so the same metal reflects black. Diffuse concrete is the honest
#    stand-in until the bake exists; flagged as a deliberate deviation.
patch_rmat(lambda n: n == 'Material.014', {'Metallic': '0', 'Roughness': '0.85',
                                            'BaseColor': '[0.62, 0.64, 0.68, 1]'})
print('shell: diffuse concrete (deviation from Metallic 1, noted)')

# 5. field of view
d = io.open(SCENE, 'r', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in d else '\n'
d = d.replace('      PerspectiveFOV: 40' + nl, '      PerspectiveFOV: 39.6' + nl, 1)
io.open(SCENE, 'w', encoding='utf-8', newline='').write(d)
print('fov 39.6; Static:false left:', d.count('Static: false'))

# 6. measure
tag = sys.argv[1] if len(sys.argv) > 1 else 'engine_rebuild'
print(run([sys.executable, os.path.join(SP, 'compare.py'), tag]).strip())
