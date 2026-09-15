# -*- coding: utf-8 -*-
"""RT-15: the surface-history switch on a curved *mirror* -- does it drag the reflection along?

rt15_surface.py found that a moving, curved reflector takes its surface's history better than its
image's (the driving car: grain 2.7% -> 1.4%), with the flat cube untouched. A sharp curved
mirror is the case that could go the other way: its reflection belongs to the world, and a
history attached to the surface would carry it along. A chrome sphere (roughness 0.05, 1.2 m)
crosses the owner's shot at 3 m/s on the cube's path, scored against its settled pose at frame
120 (stopped there, frames 150-169), as shipped, averaging nothing, and with the switch.

Usage: rt15_sphere.py [render]
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_surface as sf  # noqa: E402  -- registers curv02 and noaverage

d = sf.d
stage_run, s2 = d.stage_run, d.s2
CR, LF = chr(13) + chr(10), chr(10)
FT, K = 0.0166, 120
SPHERE = '''  - EntityID: 7311000000000000301
    TagComponent:
      Tag: MovingSphere
    TransformComponent:
      Position: [-9, 1.6, -6]
      Rotation: [0, 0, 0]
      Scale: [1.2, 1.2, 1.2]
    MeshComponent:
      Static: false
      Mesh: 8241982477996916737
      Material:
        BaseColor: [0.95, 0.95, 1, 1]
        Emissive: [0, 0, 0, 1]
        Metallic: 1
        Roughness: 0.05
        Occlusion: 1
    NativeScriptComponent:
      Script: Slider
      Fields:
        Speed: 3
        StopAfter: %g
'''


def burst(tag, stop, first, frames, variant):
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout.decode('utf-8').replace(CR, LF)
    path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    io.open(path, 'wb').write((head + (LF if not head.endswith(LF) else '') + SPHERE % stop).encode('utf-8'))
    env = dict(os.environ, BURST_SCENE=stage_run.HEAD_SCENE, BURST_CAM=s2.CAMS['owner'])
    env.pop('BURST_SLIDE', None)
    try:
        stage_run.restore()
        for shader, text in stage_run.build_variant(variant).items():
            io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
        subprocess.run([sys.executable, stage_run.BURST, tag, '--speed=0', '--stop=0.1', '--frames=%d' % frames, '--from=%d' % first],
                       cwd=stage_run.ROOT, capture_output=True, text=True, timeout=3600, errors='replace', env=env)
    finally:
        stage_run.restore()
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


img = lambda tag, k: np.asarray(Image.open(os.path.join(stage_run.SHOTS, '%s_%d.png' % (tag, k))).convert('RGB'), dtype=float)[:860]
L = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]

if __name__ == '__main__':
    arms = ['ship', 'noaverage', 'curv02']
    if 'render' in sys.argv:
        burst('rsp_ref', K * FT, 150, 20, 'ship')
        for arm in arms:
            burst('rsp_%s' % arm, 6.0, K - 4, 9, arm)
    ref = np.mean([img('rsp_ref', k) for k in range(150, 170)], axis=0)
    # The sphere is the bright-edged disc on the cube's path: find it by the settled frame's difference from a frame without it.
    errs = {f: np.abs(L(img('rsp_ship', f)) - L(ref)).mean() for f in range(K - 4, K + 5)}
    best = min(errs, key=errs.get)
    print('pose match: frame %d' % best)
    region = (250, 470, 560, 900)
    y0, y1, x0, x1 = region
    for arm in arms:
        im = Image.open(os.path.join(stage_run.SHOTS, 'rsp_%s_%d.png' % (arm, best))).convert('RGB')
        a = np.asarray(im, dtype=float)[:860]
        g = np.asarray(im.convert('L'), dtype=float)[:860]
        med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
        e = np.abs(L(a) - L(ref))[y0:y1, x0:x1]
        print('   %-10s sphere region: error %.2f (>16 %.1f%%) grain %.1f%%' % (arm, e.mean(), (e > 16).mean() * 100,
              (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
    tiles = [img('rsp_%s' % arm, best)[y0:y1, x0:x1].repeat(2, 0).repeat(2, 1) for arm in arms]
    tiles.append(ref[y0:y1, x0:x1].repeat(2, 0).repeat(2, 1))
    os.makedirs(os.path.join(stage_run.ROOT, 'build', 'rt15'), exist_ok=True)
    s2._sheet(os.path.join(stage_run.ROOT, 'build', 'rt15', 'sphere_%d.png' % best), tiles,
              ['as shipped, moving', 'no averaging', 'curved moving: surface history', 'stopped there, settled'])
