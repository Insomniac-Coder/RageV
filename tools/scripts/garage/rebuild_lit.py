"""Import the lit bake, rebuild showroom.rage on it, and measure.

Nothing to scale or override this time: every garage surface carries the
picture Cycles made of it, as emission over a black base, so the engine
neither relights it nor needs the tube strengths glTF could not carry.
"""
import subprocess, os, sys, re, io, glob, shutil

SP = r'C:\Users\ism19\Code\RageV\tools\scripts\garage'
R = r'C:\Users\ism19\Code\RageV'
G = os.path.join(R, 'SampleProject', 'assets', 'models', 'garage_lit')
SCENE = os.path.join(R, 'SampleProject', 'assets', 'scenes', 'showroom.rage')
YAML = os.path.join(R, 'tools', 'scripts', 'data', 'garage_lit.yaml')


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr


for f in glob.glob(os.path.join(G, '*.rmat')) + glob.glob(os.path.join(G, '*.rmat.meta')):
    os.remove(f)
out = run([os.path.join(R, 'build', 'bin', 'Release', 'rvimport', 'rvimport.exe'),
           'models/garage_lit/underground_garage_lit.gltf',
           '--project=' + os.path.join(R, 'SampleProject'), '--rhi=vulkan', '--out=' + YAML],
          cwd=os.path.join(R, 'build', 'bin', 'Release', 'rvimport'))
print('import: %d materials, errors=%d, entities=%d'
      % (len(glob.glob(os.path.join(G, '*.rmat'))), out.lower().count('error'),
         io.open(YAML, 'r', encoding='utf-8', errors='replace').read().count('EntityID')))

# point the migration at this subtree
mp = os.path.join(SP, 'migrate.py')
m = io.open(mp, 'r', encoding='utf-8').read()
m = re.sub(r"SUB = r'[^']*'", "SUB = r'%s'" % YAML.replace('\\', '\\\\'), m, count=1)
io.open(mp, 'w', encoding='utf-8').write(m)

shutil.copy(os.path.join(SP, 'showroom.rage.studio.bak'), SCENE)
print(run([sys.executable, mp]).strip().splitlines()[-1])
print(run([sys.executable, os.path.join(SP, 'fix1.py')]).strip().splitlines()[-1])

d = io.open(SCENE, 'r', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in d else '\n'
d = d.replace('      PerspectiveFOV: 40' + nl, '      PerspectiveFOV: 39.6' + nl, 1)
io.open(SCENE, 'w', encoding='utf-8', newline='').write(d)

# how many garage materials carry an emissive map, and whether any base
# colour survived that would let the engine light them a second time
emis = base = 0
for p in glob.glob(os.path.join(G, '*.rmat')):
    t = io.open(p, 'r', encoding='utf-8').read()
    if re.search(r'Maps:.*Emissive:', t, re.S):
        emis += 1
    if re.search(r'Maps:.*BaseColor:', t, re.S):
        base += 1
print('lit materials: %d with an emissive map, %d still carrying a base colour map' % (emis, base))

tag = sys.argv[1] if len(sys.argv) > 1 else 'engine_lit'
print(run([sys.executable, os.path.join(SP, 'compare.py'), tag]).strip())
