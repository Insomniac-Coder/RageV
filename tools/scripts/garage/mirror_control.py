# -*- coding: utf-8 -*-
"""Does a reflection carry the room's light, or only its lamps?

A chrome plate laid flat on the garage floor, so one frame holds each surface
twice -- seen directly and seen in the plate. Both go through one tonemap at one
exposure, so the pair is comparable with no cross-render correction, which is
what every earlier attempt at this question got wrong: they compared a
reflection against a *guess* at what it should be.

A metallic white mirror at roughness 0.02 reflects about 95%, so a reflection
that carries the lighting lands near parity and one that does not lands near
zero. Measured 2026-09-22 on 1427163: ceiling 71%, tube 87%, wall 106%.

    mirror_control.py [--keep]

`--keep` leaves the captures on disk; by default they are deleted, since a
number that has been read does not need its picture kept.
"""
import glob
import io
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'session_2026_09_13'))
import stage_run as sr  # noqa: E402

# The pose the regions below were chosen for. Move it and they mean nothing.
CAM = '-2.3,1.6,-2,9,0,14'
FRAME = 200

# Each pair is one surface: where it is on screen, and where its image is in
# the plate. Verified by drawing them over the frame (see --keep).
PAIRS = {
    'ceiling': ((40, 90, 1080, 1260), (600, 650, 820, 1010)),
    'tube':    ((105, 128, 700, 900), (668, 692, 700, 880)),
    'wall':    ((200, 300, 760, 1010), (480, 545, 800, 1010)),
}


def luma(patch):
    v = patch.reshape(-1, 3).mean(axis=0)
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]


def render(tag):
    if not sr.restore():
        sys.exit('the staged shader copies did not restore')
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=sr.ROOT, capture_output=True, check=True).stdout
    io.open(os.path.join(sr.SCENES, sr.HEAD_SCENE), 'wb').write(sr.mirror_control_scene(head))
    env = dict(os.environ, BURST_SCENE=sr.HEAD_SCENE, BURST_CAM=CAM)
    env.pop('BURST_SLIDE', None)
    for f in glob.glob(os.path.join(sr.SHOTS, tag + '_*.png')):
        os.remove(f)
    subprocess.run([sys.executable, os.path.join(HERE, 'burst.py'), tag,
                    '--speed=0', '--stop=0.1', '--frames=2', '--from=%d' % FRAME],
                   cwd=sr.ROOT, capture_output=True, timeout=3600, env=env)
    shots = sorted(glob.glob(os.path.join(sr.SHOTS, tag + '_*.png')))
    if not shots:
        sys.exit('no frame rendered')
    return shots[-1]


def main(argv):
    keep = '--keep' in argv
    tag = 'mirror_control'
    try:
        shot = render(tag)
        pixels = np.asarray(Image.open(shot).convert('RGB'), dtype=np.float32)
        print('surface    direct  mirrored   mirrored/direct')
        for name, (there, mirrored) in PAIRS.items():
            a = luma(pixels[there[0]:there[1], there[2]:there[3]])
            b = luma(pixels[mirrored[0]:mirrored[1], mirrored[2]:mirrored[3]])
            print('%-9s %6.1f    %6.1f       %4.0f%%' % (name, a, b, 100.0 * b / max(a, 1e-6)))
        print('a mirror of white metal reflects about 95%; near parity is right,')
        print('near zero would mean a reflected surface arrives unlit.')
        if keep:
            marked = Image.open(shot).convert('RGB')
            draw = ImageDraw.Draw(marked)
            for there, mirrored in PAIRS.values():
                for y0, y1, x0, x1 in (there, mirrored):
                    draw.rectangle([x0, y0, x1, y1], outline=(255, 0, 0), width=3)
            out = os.path.join(sr.SHOTS, tag + '_regions.png')
            marked.save(out)
            print('regions drawn over the frame:', out)
    finally:
        print('staged copies restored:', sr.restore())
        for name in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            path = os.path.join(sr.SCENES, name)
            if os.path.exists(path):
                os.remove(path)
        if not keep:
            for f in glob.glob(os.path.join(sr.SHOTS, tag + '_*.png')):
                os.remove(f)


main(sys.argv[1:])
