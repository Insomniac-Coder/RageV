# -*- coding: utf-8 -*-
"""Measure a change against a 16-ray truth render of the same shot.

The instrument this project never had. Three arms per case, all through the same
harness so the camera, the scene and the clock are identical:

  truth    the shipped shaders with the reflection trace forced to 16 rays a
           texel -- slow, and the answer the one-ray picture is trying to be
  shipped  the shaders as they are at HEAD
  fixed    the shaders as they are in the working tree

Scored on the case's own region: how far each arm sits from the truth, and how
many bright specks it carries against the truth's own count.

Usage: truth_test.py [garage|cube|car|all]
"""
import glob
import io
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run as sr  # noqa: E402

SHADERS = ['reflection_trace.rvshader', 'reflection_resolve.rvshader',
           'reflection_accumulate.rvshader']
MANY = ('\tvec3 radianceSum = radiance;', '\trays = 16;\n\tvec3 radianceSum = radiance;')

CAMS = {'garage': '-2.3,0.72,-2,11,0,4', 'close': '-4.3,1.0,-2,3.2,25,8'}
CASES = {
    'garage': dict(cam='garage', first=150, opts={},
                   regions={'wet floor': (600, 900, 0, 1600)}),
    'cube': dict(cam='garage', first=120, opts=dict(cube=(-9.0, 3.0, 6.0)),
                 regions={'cube face': (300, 420, 640, 860), 'floor': (560, 860, 0, 1600)}),
    'car': dict(cam='close', first=75, opts=dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0'),
                regions={'car body': (380, 760, 250, 1250), 'floor': (760, 860, 0, 1600)}),
}


def head_text(name):
    raw = subprocess.run(['git', 'show', 'HEAD:RageVEditor/assets/shaders/' + name],
                         cwd=sr.ROOT, capture_output=True, check=True).stdout
    return raw.decode('utf-8')


def stage(arm):
    """Put the arm's shaders where the runtime reads them."""
    if not sr.restore():
        sys.exit('the staged copies did not restore')
    if arm == 'fixed':
        return                                  # the working tree, already staged
    for name in SHADERS:
        text = head_text(name)
        if arm == 'truth' and name == 'reflection_trace.rvshader':
            if text.count(MANY[0]) != 1:
                sys.exit('the ray count anchor moved')
            text = text.replace(MANY[0], MANY[1])
        io.open(os.path.join(sr.STAGED_DIR, name), 'w', encoding='utf-8',
                newline='').write(text)


def render(case, arm):
    c = CASES[case]
    os.environ['BURST_CAM'] = CAMS[c['cam']]
    tag = 'tt_%s_%s' % (case, arm)
    stage(arm)
    sr.run_arms([(tag, 'ship', ['--reflection-noise-blur=off'],
                  dict(frames=2, first=c['first'], **c['opts']))], stage_first=False) \
        if 'stage_first' in sr.run_arms.__code__.co_varnames else _burst(tag, c)
    return tag


def _burst(tag, c):
    """run_arms restages from source, so the burst is driven directly here."""
    burst = os.path.join(sr.ROOT, 'tools', 'scripts', 'garage', 'burst.py')
    env = dict(os.environ, BURST_SCENE=sr.HEAD_SCENE)
    for key in ('BURST_SLIDE',):
        env.pop(key, None)
        if c['opts'].get(key):
            env[key] = c['opts'][key]
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=sr.ROOT, capture_output=True, check=True).stdout
    scene = sr.moving_cube_scene(head, *c['opts']['cube']) if c['opts'].get('cube') else head
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(scene)
    for f in glob.glob(os.path.join(sr.SHOTS, 'rt5b_' + tag + '_*.png')):
        os.remove(f)
    subprocess.run([sys.executable, burst, 'rt5b_' + tag, '--speed=0', '--stop=0.1',
                    '--frames=2', '--from=%d' % c['first'],
                    '--extra=--reflection-noise-blur=off'],
                   cwd=sr.ROOT, capture_output=True, timeout=3600, env=env)


def shot(tag, k):
    return os.path.join(sr.SHOTS, 'rt5b_%s_%d.png' % (tag, k))


def px(path, R):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)[R[0]:R[1], R[2]:R[3]]


def specks(path, R):
    im = Image.open(path).convert('L').crop((R[2], R[0], R[3], R[1]))
    x = np.asarray(im, dtype=np.float32)
    y = np.asarray(im.filter(ImageFilter.MedianFilter(3)), dtype=np.float32)
    return int(((x - y) > 16).sum())


def run(case):
    c = CASES[case]
    tags = {arm: render(case, arm) for arm in ('truth', 'shipped', 'fixed')}
    k = c['first']
    print(case)
    for name, R in c['regions'].items():
        t = px(shot(tags['truth'], k), R)
        ts = specks(shot(tags['truth'], k), R)
        print('   %-12s the truth carries %d bright specks' % (name, ts))
        for arm in ('shipped', 'fixed'):
            a = px(shot(tags[arm], k), R)
            print('   %-12s %-8s %.3f from the truth, %d specks'
                  % ('', arm, np.abs(a - t).mean(), specks(shot(tags[arm], k), R)))


if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else 'all'
    try:
        for case in (list(CASES) if which == 'all' else [which]):
            run(case)
    finally:
        print('staged copies restored:', sr.restore())
        for f in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            p = os.path.join(sr.SCENES, f)
            if os.path.exists(p):
                os.remove(p)
