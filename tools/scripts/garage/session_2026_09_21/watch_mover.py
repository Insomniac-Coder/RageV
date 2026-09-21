# -*- coding: utf-8 -*-
"""Play the moving car and the moving cube twice: as it ships, then with the
reflection memory refused on surfaces that move on their own.

Two windows per case, back to back, long enough to watch. Nothing is captured
and nothing is scored -- this is for the eye.

Usage: watch_mover.py [car|cube|both] [seconds]
"""
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run as sr  # noqa: E402

ACC = 'reflection_accumulate.rvshader'
N = chr(10)
BASE = ('\t\tif (PositionLane())' + N + '\t\t{' + N + '\t\t\tc = atSurface;' + N
        + '\t\t\thave = haveSurface;' + N + '\t\t\tchoice = have ? 0.5 : 0.0;' + N + '\t\t}' + N)
sr.VARIANTS['nomirrorhist'] = [(ACC, BASE, BASE
                                + '\t\tif (!PositionLane() && dot(objectShift, objectShift) > 0.0)' + N
                                + '\t\t\thave = false;' + N)]

CAMS = {'car': '-4.3,1.0,-2,3.2,25,8', 'cube': '-2.3,0.72,-2,11,0,4'}
SLIDER = ('    NativeScriptComponent:' + N + '      Script: Slider' + N + '      Fields:' + N
          + '        Speed: %g' + N + '        StopAfter: %g' + N)


def scene_for(case, seconds):
    """HEAD's showroom with the case's mover in it, driving for the whole watch."""
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=sr.ROOT, capture_output=True, check=True).stdout
    if case == 'cube':
        return sr.moving_cube_scene(head, -9.0, 1.2, seconds)
    text = head.decode('utf-8').replace(chr(13) + N, N)
    out, count = [], 0
    for part in re.split(r'(?=\n  - EntityID: )', text):
        tag = re.search(r'\n      Tag: (.*)', part)
        if tag and tag.group(1).strip() == 'porsche_992_gt3_r':
            body = part.rstrip(N)
            part = body + N + (SLIDER % (1.0, seconds)).rstrip(N) + part[len(body):]
            count += 1
        out.append(part)
    if count != 1:
        sys.exit('the car matched %d entities' % count)
    return ''.join(out).encode('utf-8')


def still_camera(raw):
    """Swap the scene's own camera script out, or --camera is ignored (the
    trap burst.py pays for: a run comes out at the orbit script's pose)."""
    text = raw.decode('utf-8').replace(chr(13) + N, N)
    pattern = ('    ManagedScriptComponent:' + N + '      Script: ShowroomCamera' + N
               + '      Fields:' + N + '(?:        .*' + N + ')+')
    old = re.search(pattern, text)
    if not old:
        sys.exit('no ShowroomCamera block to swap out')
    keep = ('    NativeScriptComponent:' + N + '      Script: Slider' + N + '      Fields:' + N
            + '        Speed: 0' + N + '        StopAfter: 0.1' + N)
    return (text[:old.start()] + keep + text[old.end():]).encode('utf-8')


def play(case, variant, seconds):
    path = os.path.join(sr.SCENES, sr.HEAD_SCENE)
    io.open(path, 'wb').write(still_camera(scene_for(case, seconds)))
    try:
        sr.restore()
        for shader, text in sr.build_variant(variant).items():
            io.open(os.path.join(sr.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
        print('%s -- %s' % (case, 'as it ships' if variant == 'ship'
                            else 'reflection memory refused on movers'))
        subprocess.run([os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime',
                                     'RageVRuntime.exe'),
                        '--project=' + sr.PROJECT, '--scene=scenes/' + sr.HEAD_SCENE,
                        '--rhi=vulkan', '--render-defaults=off', '--vsync=on',
                        '--width=1600', '--height=900', '--import-cache=off',
                        '--camera=' + CAMS[case], '--benchmark=%d' % int(seconds * 60)],
                       cwd=os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime'),
                       timeout=3600)
    finally:
        sr.restore()
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else 'both'
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
    for case in (['car', 'cube'] if which == 'both' else [which]):
        for variant in ('ship', 'nomirrorhist'):
            play(case, variant, secs)
