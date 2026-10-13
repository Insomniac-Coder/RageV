#!/usr/bin/env python3
"""The reference renderer's white furnace (RT2-2b step 4).

Twenty-two spheres under a sky of radiance one (scenes/furnace_spheres.rage):
eleven metal ones (albedo 1, so F0 = 1) and eleven white dielectrics (albedo 1,
specular 0.5), at roughness 0, 0.1 ... 1, a hundred metres apart so none sees
another (at ten, a mirror's image of its neighbour moved an 8x8 block by 2%). A convex surface under a uniform sky sees only the sky, so each pixel
must read exactly the material's directional albedo at that pixel's view
angle -- the integral of f cos over the hemisphere -- and nothing else. This
renders every sphere with `--reference-render`, asks the reference for N.V at
each pixel (`--reference-debug=nv`), computes the directional albedo of the
same material model for that N.V here, independently, and compares them pixel
by pixel (8x8 blocks, so the grain does not decide it).

The material is the reference's own definition: GGX with the height-correlated
Smith term, Schlick Fresnel with F0 = mix(0.08 x specular, albedo, metallic),
the diffuse weighted by (1 - F)(1 - metallic), roughness floored at 0.045. Its
directional albedo is estimated here by Monte Carlo with GGX's visible normals
(the specular term's weight is then F G2 / G1) and cosine samples (the
diffuse's weight is (1 - F) x albedo), two hundred thousand samples per N.V (0.1% of grain) -- an
independent route to the same integral the renderer estimates its own way.

What this measures is the renderer, not the material: a perfect reflector
under a uniform sky should read 1.0, and the single-scattering GGX model reads
less at high roughness (its multiple-scattering energy is missing) -- that
loss is reported beside the result, and closing it is RT2-21b's (energy
compensation), not this check's.

Run from the repository root:

    python tools/scripts/check_reference_furnace.py [--samples 4096] [--keep]
"""
import argparse
import os
import shutil
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNTIME = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
OUT = os.path.join(ROOT, 'build', 'reference_furnace')
ROUGHNESS = [round(i * 0.1, 2) for i in range(11)]
TOLERANCE = 0.02


def render(index_x, row_z, samples, debug, tag):
    out = os.path.join(OUT, tag + '.png')
    args = [os.path.join(RUNTIME, 'RageVRuntime.exe'),
            '--project=' + os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject'),
            '--rhi=vulkan', '--render-defaults=off', '--vsync=off', '--width=512', '--height=512',
            '--aa=none', '--scene=scenes/furnace_spheres.rage',
            '--camera=%g,0,%g,2.2,0,0' % (100 * index_x, row_z),
            '--reference-render=%d' % samples, '--screenshot-frame=settled',
            '--screenshot=' + out, '--capture-signals=reference']
    if debug:
        args.append('--reference-debug=' + debug)
    subprocess.run(args, cwd=RUNTIME, stdin=subprocess.DEVNULL, capture_output=True, timeout=1800)
    path = os.path.join(OUT, tag + '_reference0.npy')
    if not os.path.exists(path):
        sys.exit('no capture for ' + tag)
    return np.load(path)


def schlick(cos_theta, f0):
    return f0 + (1.0 - f0) * np.clip(1.0 - cos_theta, 0.0, 1.0) ** 5


def smith_lambda(cos_theta, alpha):
    cos2 = np.maximum(cos_theta * cos_theta, 1e-12)
    return 0.5 * (-1.0 + np.sqrt(1.0 + alpha * alpha * (1.0 - cos2) / cos2))


def directional_albedo(mu, roughness, metallic, rng, count=200_000):
    """The integral of f cos over the hemisphere at N.V = mu, for albedo 1."""
    alpha = max(roughness, 0.045) ** 2
    f0 = 1.0 * metallic + 0.08 * 0.5 * (1.0 - metallic)
    sin_v = np.sqrt(max(0.0, 1.0 - mu * mu))
    V = np.array([sin_v, 0.0, mu])

    # The specular lobe by GGX's visible normals (Heitz 2018).
    u1, u2 = rng.random(count), rng.random(count)
    Vh = np.array([alpha * V[0], alpha * V[1], V[2]])
    Vh /= np.linalg.norm(Vh)
    lensq = Vh[0] ** 2 + Vh[1] ** 2
    T1 = np.array([-Vh[1], Vh[0], 0.0]) / np.sqrt(lensq) if lensq > 0 else np.array([1.0, 0.0, 0.0])
    T2 = np.cross(Vh, T1)
    r = np.sqrt(u1)
    phi = 2.0 * np.pi * u2
    t1 = r * np.cos(phi)
    t2 = r * np.sin(phi)
    s = 0.5 * (1.0 + Vh[2])
    t2 = (1.0 - s) * np.sqrt(1.0 - t1 * t1) + s * t2
    Nh = (t1[:, None] * T1 + t2[:, None] * T2
          + np.sqrt(np.maximum(0.0, 1.0 - t1 * t1 - t2 * t2))[:, None] * Vh)
    H = np.stack([alpha * Nh[:, 0], alpha * Nh[:, 1], np.maximum(0.0, Nh[:, 2])], axis=1)
    H /= np.linalg.norm(H, axis=1, keepdims=True)
    VdotH = H @ V
    L = 2.0 * VdotH[:, None] * H - V
    lz = L[:, 2]
    above = lz > 0.0
    g1v = 1.0 / (1.0 + smith_lambda(mu, alpha))
    g2 = 1.0 / (1.0 + smith_lambda(mu, alpha) + smith_lambda(np.where(above, lz, 1.0), alpha))
    specular = np.where(above, schlick(np.maximum(VdotH, 0.0), f0) * g2 / g1v, 0.0).mean()

    # The diffuse by the cosine.
    if metallic >= 1.0:
        return specular
    u1, u2 = rng.random(count), rng.random(count)
    r = np.sqrt(u1)
    phi = 2.0 * np.pi * u2
    L = np.stack([r * np.cos(phi), r * np.sin(phi), np.sqrt(np.maximum(0.0, 1.0 - u1))], axis=1)
    Hd = L + V
    Hd /= np.linalg.norm(Hd, axis=1, keepdims=True)
    diffuse = ((1.0 - schlick(np.maximum(Hd @ V, 0.0), f0)) * (1.0 - metallic)).mean()
    return specular + diffuse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--samples', type=int, default=4096)
    parser.add_argument('--keep', action='store_true', help='keep the captures in build/reference_furnace')
    options = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(7)
    mus = np.linspace(0.05, 1.0, 40)

    failures = 0
    print('%-26s %9s %9s %9s %9s' % ('sphere', 'worst', 'mean err', 'albedo', 'albedo'))
    print('%-26s %9s %9s %9s %9s' % ('', 'block', '', 'at N.V=1', 'at 0.2'))
    for row, (metallic, name) in enumerate([(1.0, 'metal'), (0.0, 'white')]):
        for i, roughness in enumerate(ROUGHNESS):
            tag = '%s_%02d' % (name, i)
            radiance = render(i, -100 * row, options.samples, None, tag)[..., :3].mean(axis=2)
            nv = render(i, -100 * row, 256, 'nv', tag + '_nv')
            table = np.array([directional_albedo(mu, roughness, metallic, rng) for mu in mus])

            # The sphere's own pixels, whole ones, away from the limb -- the
            # neighbours at the frame's edges are not this sphere.
            h, w = radiance.shape
            yy, xx = np.mgrid[0:h, 0:w]
            centre = (xx - w / 2) ** 2 + (yy - h / 2) ** 2 < (0.3 * w) ** 2
            use = centre & (nv[..., 1] > 0.999) & (nv[..., 0] > 0.15)
            expected = np.interp(nv[..., 0], mus, table)

            block = 8
            H, W = h // block, w // block
            def blocks(a):
                return a[:H * block, :W * block].reshape(H, block, W, block)
            m = blocks(use)
            full = m.all(axis=(1, 3))
            got = blocks(radiance).mean(axis=(1, 3))
            want = blocks(expected).mean(axis=(1, 3))
            err = np.abs(got / want - 1.0)[full]
            worst = err.max() if err.size else float('nan')
            mean = (got / want - 1.0)[full].mean() if err.size else float('nan')
            ok = worst <= TOLERANCE
            failures += 0 if ok else 1
            print('%-26s %8.2f%% %8.2f%% %9.4f %9.4f %s' % (
                '%s roughness %.1f' % (name, roughness), 100 * worst, 100 * mean,
                table[-1], np.interp(0.2, mus, table), 'ok' if ok else 'FAIL'))

    if not options.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print('\n%s: every sphere within %.0f%% of its directional albedo, block by block'
          % ('OK' if failures == 0 else 'FAIL (%d)' % failures, 100 * TOLERANCE))
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
