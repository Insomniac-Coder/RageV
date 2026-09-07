"""Import the PBR-baked garage, rebuild showroom.rage on it, and measure it.

One command, so every iteration is the same iteration: rvimport from its own
folder, the studio scene migrated onto the new subtree, Static flags, the
artist's field of view, and the render at the ground truth's framing with the
numbers. Nothing is patched into the materials any more: emission strengths
arrive through KHR_materials_emissive_strength, which the importer reads, and
the shell is left as the file says it is (Metallic 1) until a picture says
otherwise.

    python tools/scripts/garage/rebuild_pbr.py <tag> [folder gltf] [--bake] [--no-import]

The optional folder and file name point it at another export in
assets/models -- used once to re-import garage_baked through the fixed
importer before the PBR bake had finished.
"""
import subprocess, os, sys, re, io, glob, shutil

SP = r'C:\Users\ism19\Code\RageV\tools\scripts\garage'
R = r'C:\Users\ism19\Code\RageV'
_POS = [a for a in sys.argv[1:] if not a.startswith('--')]
FOLDER = _POS[1] if len(_POS) > 1 else 'garage_pbr'
GLTF = _POS[2] if len(_POS) > 2 else 'underground_garage_pbr.gltf'
G = os.path.join(R, 'SampleProject', 'assets', 'models', FOLDER)
SCENE = os.path.join(R, 'SampleProject', 'assets', 'scenes', 'showroom.rage')
YAML = os.path.join(R, 'tools', 'scripts', 'data', FOLDER + '.yaml')


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr


# 1. fresh materials from the export -- skipped with `--no-import` when the
#    export has not changed, which saves the four minutes of mesh cooking a
#    lighting iteration does not need
out = ''
if '--no-import' in sys.argv:
    print('import: skipped (--no-import)')
else:
  for f in glob.glob(os.path.join(G, '*.rmat')) + glob.glob(os.path.join(G, '*.rmat.meta')):
    os.remove(f)
  out = run([os.path.join(R, 'build', 'bin', 'Release', 'rvimport', 'rvimport.exe'),
             'models/%s/%s' % (FOLDER, GLTF),
             '--project=' + os.path.join(R, 'SampleProject'), '--rhi=vulkan', '--out=' + YAML],
            cwd=os.path.join(R, 'build', 'bin', 'Release', 'rvimport'))
mats = glob.glob(os.path.join(G, '*.rmat'))
print('import: %d materials, errors=%d, entities=%d'
      % (len(mats), out.lower().count('error'),
         io.open(YAML, 'r', encoding='utf-8', errors='replace').read().count('EntityID')))
for ln in out.splitlines():
    if 'error' in ln.lower() or 'warn' in ln.lower():
        print('   ', ln.strip()[:160])

# what the emitters arrived as
for p in sorted(mats):
    t = io.open(p, 'r', encoding='utf-8').read()
    m = re.search(r'^Emissive: \[([^\]]*)\]', t, re.M)
    if m:
        v = [float(x) for x in m.group(1).split(',')]
        if max(v[:3]) > 1.0:
            print('   emitter %-40s %s' % (os.path.basename(p)[:40], [round(x, 2) for x in v[:3]]))

# 1b. the tubes' radiance. The .blend says 16 (light) and 12 (light2), and the
#     importer carries that; the owner wants the car to reflect them harder
#     (2026-09-05 night), and the paint reflects four percent of a surface's
#     radiance at normal incidence. An absolute value, not a gain, so running
#     this twice sets the same number. The post profile's ReflectionFloor must
#     sit at or above it or the glass's in-line rays are clamped back.
TUBE_RADIANCE = 100.0
TUBE_COLOUR = (0.3419134, 0.6307567, 1.0)
for p_ in glob.glob(os.path.join(G, '*.rmat')):
    m = re.match(r'^underground_garage_pbr_\d+_(light2?)\.rmat$', os.path.basename(p_))
    if not m:
        continue
    raw = io.open(p_, 'r', encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    out_ = []
    for ln in raw.replace('\r\n', '\n').split('\n'):
        if ln.startswith('Emissive: ['):
            ln = 'Emissive: [%.4g, %.4g, %.4g, 1]' % tuple(c * TUBE_RADIANCE for c in TUBE_COLOUR)
        out_.append(ln)
    while out_ and not out_[-1].strip():
        out_.pop()
    io.open(p_, 'w', encoding='utf-8', newline='').write(nl.join(out_) + nl)
    print('   tube %-8s radiance %g' % (m.group(1), TUBE_RADIANCE))

# 2. the scene
shutil.copy(os.path.join(SP, 'showroom.rage.studio.bak'), SCENE)
print(run([sys.executable, os.path.join(SP, 'migrate.py'), YAML]).strip().splitlines()[-1])
print(run([sys.executable, os.path.join(SP, 'static_flags.py')]).strip().splitlines()[-1])

d = io.open(SCENE, 'r', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in d else '\n'
# 3. field of view: the 44.6 mm of the page's wide still (compare.py)
sys.path.insert(0, SP)
import compare
d = d.replace('      PerspectiveFOV: 40' + nl, '      PerspectiveFOV: %g' % compare.FOV + nl, 1)
io.open(SCENE, 'w', encoding='utf-8', newline='').write(d)
print('fov %g; Static:false left:' % compare.FOV, d.count('Static: false'))

# 4. the engine's own lighting bake, when asked (`--bake` anywhere in argv).
#    A reflection or refraction ray that hits a Static surface reads the
#    irradiance field for that surface's light, so without a field for this
#    room every mirror ray into the garage came back black. `--bake=on` only
#    writes what is missing; `force` is what actually solves (HANDOFF), and it
#    needs frames to settle in -- 8000 is the number that worked before.
if '--bake' in sys.argv:
    out = run([os.path.join(R, 'build', 'bin', 'Release', 'RageVRuntime', 'RageVRuntime.exe'),
               '--project=' + os.path.join(R, 'SampleProject'), '--scene=scenes/showroom.rage',
               '--rhi=vulkan', '--bake=force', '--render-defaults=off', '--benchmark=8000',
               '--vsync=off', '--width=1280', '--height=720'],
              cwd=os.path.join(R, 'build', 'bin', 'Release', 'RageVRuntime'))
    lines = [ln for ln in out.splitlines() if 'bake' in ln.lower() or 'field' in ln.lower() or 'solve' in ln.lower()]
    print('engine bake: %d lines mention it' % len(lines))
    for ln in lines[-8:]:
        print('   ', ln.strip()[:150])
    baked = os.path.join(R, 'SampleProject', 'assets', 'baked', 'showroom')
    if os.path.isdir(baked):
        for f in sorted(os.listdir(baked)):
            fp = os.path.join(baked, f)
            print('    %-48s %8d bytes  %s' % (f, os.path.getsize(fp), __import__('time').strftime('%H:%M:%S', __import__('time').localtime(os.path.getmtime(fp)))))

# 5. measure
tag = next((a for a in sys.argv[1:] if not a.startswith('--')), 'engine_pbr')
print(run([sys.executable, os.path.join(SP, 'compare.py'), tag]).strip())
