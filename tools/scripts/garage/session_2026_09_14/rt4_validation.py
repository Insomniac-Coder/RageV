# -*- coding: utf-8 -*-
"""RT-4: the reflection chain before the lit pass, under the Vulkan validation layers.

The garage (HEAD's showroom.rage, parked, 72 frames) and the bridge's Deck camera
(64 frames), each run once with --validation=on, the working tree's shaders staged
the way stage_run stages them. Prints every validation message kind and its count;
the bridge's two known kinds (HANDOFF 2026-09-13, "Validation, where it stands")
are expected, anything else is this change's.

Usage: rt4_validation.py
"""
import collections, io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402

ROOT, RT = stage_run.ROOT, stage_run.RT
SHOTS = os.path.join(stage_run.OUT, 'rt4_validation')


def kinds(text):
    found = collections.Counter()
    for line in text.splitlines():
        m = re.search(r'(VUID-[A-Za-z0-9_\-]+|UNASSIGNED-[A-Za-z0-9_\-]+)', line)
        if m:
            found[m.group(1)] += 1
        elif re.search(r'validation', line, re.I) and re.search(r'error|warning', line, re.I):
            found[line.strip()[:120]] += 1
    return found


def run(name, scene, camera, first, count):
    os.makedirs(SHOTS, exist_ok=True)
    cmd = [os.path.join(RT, 'RageVRuntime.exe'), '--project=' + os.path.join(ROOT, 'SampleProject'),
           '--scene=' + scene, '--rhi=vulkan', '--render-defaults=off', '--vsync=off',
           '--width=1600', '--height=900', '--frame-time=0.0166', '--import-cache=off',
           '--screenshot=' + os.path.join(SHOTS, name + '.png'),
           '--screenshot-frame=%d' % first, '--screenshot-count=%d' % count, '--validation=on']
    if camera:
        cmd.append('--camera=' + camera)
    p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True, timeout=3600, errors='replace')
    got = [f for f in os.listdir(SHOTS) if re.match(re.escape(name) + r'_\d+\.png$', f)]
    found = kinds(p.stdout + '\n' + p.stderr)
    print('%s: exit %d, %d frames written, %d validation messages' % (name, p.returncode, len(got), sum(found.values())))
    for kind, n in found.most_common():
        print('   %5d  %s' % (n, kind))
    if not got:
        print(p.stdout[-1500:], p.stderr[-1500:])


staged = stage_run.build_variant('ship')
head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                      cwd=ROOT, capture_output=True, check=True).stdout
try:
    io.open(os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE), 'wb').write(head)
    for shader, text in staged.items():
        io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
    run('garage', 'scenes/' + stage_run.HEAD_SCENE, stage_run.__dict__.get('CAM'), 1, 72)
    run('bridge_deck', 'scenes/GoldenGateDemo.rage', '0,76.4,950,0.01,0,0', 1, 64)
finally:
    print('staged copies restored and identical to source:', stage_run.restore())
    for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
        try:
            os.remove(os.path.join(stage_run.SCENES, f))
        except OSError:
            pass
