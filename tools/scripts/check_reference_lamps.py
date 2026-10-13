#!/usr/bin/env python3
"""The reference renderer's lamp shapes against their point (RT2-2b step 4).

A uniformly glowing sphere lights every point that sees all of it exactly as a
point of the same intensity at its centre would -- the irradiance of a
Lambertian sphere is the point's. The reference gives a lamp of radius r the
frame's total light (radiance I / (pi r^2) over its surface), so on a matte
floor below it the sphere and the point must give the same picture, and any
difference is the sampling: the cone the sphere subtends, its density, the
weighting against the path. Two scenes, identical but for the lamp's radius
(scenes/lamp_point.rage, scenes/lamp_sphere.rage, 0 and 0.5 m, 3 m over the
floor), compared block by block (8x8) over every lit block.

Run from the repository root:

    python tools/scripts/check_reference_lamps.py [--samples 4096] [--keep]
"""
import argparse
import os
import shutil
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNTIME = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
OUT = os.path.join(ROOT, 'build', 'reference_lamps')
TOLERANCE = 0.01


def render(scene, samples):
    out = os.path.join(OUT, scene + '.png')
    subprocess.run([os.path.join(RUNTIME, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
                    '--rhi=vulkan', '--render-defaults=off', '--vsync=off', '--width=800', '--height=450',
                    '--aa=none', '--scene=scenes/%s.rage' % scene, '--reference-render=%d' % samples,
                    '--screenshot-frame=settled', '--screenshot=' + out, '--capture-signals=reference'],
                   cwd=RUNTIME, stdin=subprocess.DEVNULL, capture_output=True, timeout=1800)
    path = os.path.join(OUT, scene + '_reference0.npy')
    if not os.path.exists(path):
        sys.exit('no capture for ' + scene)
    return np.load(path)[..., :3].mean(axis=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--samples', type=int, default=4096)
    parser.add_argument('--keep', action='store_true', help='keep the captures in build/reference_lamps')
    options = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)

    point = render('lamp_point', options.samples)
    sphere = render('lamp_sphere', options.samples)
    block = 8
    H, W = point.shape[0] // block, point.shape[1] // block

    def blocks(a):
        return a[:H * block, :W * block].reshape(H, block, W, block)
    lit = blocks(point > point.max() * 0.02).all(axis=(1, 3))
    ratio = blocks(sphere).mean(axis=(1, 3))[lit] / blocks(point).mean(axis=(1, 3))[lit]
    worst = np.abs(ratio - 1.0).max()
    print('lit blocks %d, sphere over point: mean %.4f, min %.4f, max %.4f'
          % (lit.sum(), ratio.mean(), ratio.min(), ratio.max()))
    if not options.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    ok = worst <= TOLERANCE
    print('%s: the sphere lamp lights the floor as its point does, within %.0f%% a block'
          % ('OK' if ok else 'FAIL', 100 * TOLERANCE))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
