"""Render the garage from its AUTHORED camera (the ShowroomCamera script removed), parked, frame
240, with the build and project of a given tree -- so builds from different dates can be compared
at one view. usage: render_authored.py <tree root> <out.png> [engine flags ...]"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io, os, re, shutil, subprocess, sys

root, out, flags = sys.argv[1], sys.argv[2], sys.argv[3:]
# A staged arm (main tree only): watch_arm.py VARIANTS, staged the way spin_measure stages them.
stage_name = next((f.split('=', 1)[1] for f in flags if f.startswith('--stage=')), None)
flags = [f for f in flags if not f.startswith('--stage=')]
sm = None
# The main tree's runtime reads STAGED shader copies: always sync them from the source (and stage
# an arm on top when asked), or a source edit never reaches the engine.
if os.path.normcase(os.path.abspath(root)) == os.path.normcase(os.path.abspath((_RAGEV + ''))):
    sys.path.insert(0, os.path.join(root, 'tools', 'scripts', 'garage'))
    import spin_measure as sm
scenes = os.path.join(root, 'SampleProject', 'Assets', 'scenes')
text = io.open(os.path.join(scenes, 'showroom.rage'), 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in text else '\n'
t = text.replace('\r\n', '\n')
block = re.search('    ManagedScriptComponent:\n      Script: ShowroomCamera\n      Fields:\n(?:        .*\n)+', t)
if not block:
    sys.exit('no ShowroomCamera block')
t = t[:block.start()] + t[block.end():]
# --tube-length=L: every tube lamp's length set to L (22fc27c made them 3.07 m).
length = next((f.split('=', 1)[1] for f in flags if f.startswith('--tube-length=')), None)
flags = [f for f in flags if not f.startswith('--tube-length=')]
if length is not None:
    n = t.count('SourceLength: 3.07')
    t = t.replace('SourceLength: 3.07', 'SourceLength: ' + length)
    print('tube lamps set to length', length, '(%d lamps)' % n)
temp = 'authored_view.rage'
io.open(os.path.join(scenes, temp), 'wb').write(t.replace('\n', nl).encode('utf-8'))
# The bake is keyed by the scene's name: give the copy the showroom's own.
baked = os.path.join(root, 'SampleProject', 'Assets', 'baked')
copied = []
src = os.path.join(baked, 'showroom')
dst = os.path.join(baked, 'authored_view')
if os.path.isdir(src) and not os.path.exists(dst):
    shutil.copytree(src, dst)
    copied.append(dst)
exe_dir = os.path.join(root, 'build', 'bin', 'Release', 'RageVRuntime')
if sm:
    if not sm.sr.restore():
        sys.exit('the staged shader copies did not restore')
    if stage_name:
        sm.stage(stage_name)
try:
    p = subprocess.run([os.path.join(exe_dir, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(root, 'SampleProject', 'SampleProject.rvproject'),
                        '--scene=scenes/' + temp, '--rhi=vulkan', '--render-defaults=off',
                        '--width=1600', '--height=900', '--import-cache=off', '--vsync=off',
                        '--frame-time=0.016666667', '--screenshot-frame=240', '--screenshot-count=1',
                        '--screenshot=' + out, '--benchmark=242'] + flags,
                       cwd=exe_dir, capture_output=True, text=True, errors='replace', timeout=1800)
    log = p.stdout + p.stderr
    for line in log.splitlines():
        if any(k in line.lower() for k in ('bake', 'baked', 'error', 'fail')) and 'irradiance fill' not in line:
            print(line.strip()[:160])
finally:
    if sm:
        sm.sr.restore()
    for f in [os.path.join(scenes, temp), os.path.join(scenes, temp + '.meta')]:
        try:
            os.remove(f)
        except OSError:
            pass
    for d in copied:
        shutil.rmtree(d, ignore_errors=True)
print('wrote', out, os.path.exists(out))
