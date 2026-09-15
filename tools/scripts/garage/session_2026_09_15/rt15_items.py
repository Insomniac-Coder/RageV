# -*- coding: utf-8 -*-
"""RT-15, the three items as built, one at a time, against each moving object's settled pose.

  1  the history choice: on a curved reflector moving on its own, both histories, and the
     surface's where it sits clearly nearer this frame's picture
  2  the struck point's travel: the image history looked for where a moving reflected
     object's image stood last frame
  3  the young blur on curved reflectors moving on their own (--reflection-moving-blur)

Arms (the build as it stands; items switched off by staging the accumulator):
  before   1 and 2 staged off, blur 0     -- must reproduce HEAD's frames bit for bit
  i1       1 only
  i12      1 and 2
  blur6    all three, radius 6
  ship     all three, radius 12 (the build's default)
  noavg    no averaging at all (the noise reference)

Cases: the car driving at the close-up (pose frame 75), the chrome cube (roughness 0.12, flat)
and the chrome sphere (0.05, curved) crossing the owner's shot (pose frame 120). Each against
the settled reference already rendered by HEAD this morning (r15_car_ref, r15_cube_ref, rsp_ref)
-- nothing moves in those frames, so the build does not enter.

Usage: rt15_items.py [render] [car|cube|sphere ...]
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
import rt15_diag as d  # noqa: E402  -- registers noaverage
import rt15_sphere as sp  # noqa: E402

stage_run, s2 = d.stage_run, d.s2
ACC = d.ACC
N = '\n'
I1_OFF = (ACC, '\t\tif (have && haveSurface && g_MovingCurved)' + N, '\t\tif (false)' + N)
I2_OFF = (ACC, '\tif (u_Reflection.Change.y < 0.5)' + N + '\t\treturn vec3(0.0);' + N,
          '\tif (true)' + N + '\t\treturn vec3(0.0);' + N)
stage_run.VARIANTS['i1off'] = [I1_OFF]
stage_run.VARIANTS['i2off'] = [I2_OFF]
stage_run.VARIANTS['i12off'] = [I1_OFF, I2_OFF]
NOBLUR = ['--reflection-moving-blur=0']
ARMS = [('before', 'i12off', NOBLUR), ('i1', 'i2off', NOBLUR), ('i12', 'ship', NOBLUR),
        ('blur6', 'ship', ['--reflection-moving-blur=6']), ('ship', 'ship', []), ('noavg', 'noaverage', NOBLUR)]
FT = 0.0166
CASES = {
    'car': dict(cam='close', k=75, regions={'car body': (380, 760, 250, 1250), 'floor': (760, 860, 0, 1600)},
                head='rt5b_r15_car_ship', ref=lambda: np.load(os.path.join(stage_run.OUT, 'r15_car_ref.npy'))[:860]),
    'cube': dict(cam='owner', k=120, regions={'cube face': (300, 420, 640, 860), 'floor': (560, 860, 0, 1600)},
                 head='rt5b_r15_cube_ship', ref=lambda: np.load(os.path.join(stage_run.OUT, 'r15_cube_ref.npy'))[:860]),
    'sphere': dict(cam='owner', k=120, regions={'sphere': (250, 470, 560, 900), 'floor': (560, 860, 0, 1600)},
                   head='rsp_ship', ref=lambda: np.mean([sp.img('rsp_ref', f) for f in range(150, 170)], axis=0)),
}
S = lambda tag, k: os.path.join(stage_run.SHOTS, '%s_%d.png' % (tag, k))
L = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
OUT = os.path.join(stage_run.ROOT, 'build', 'rt15')


def sphere_burst(tag, variant, flags, first, frames):
    """rt15_sphere.burst with flags: the chrome sphere added to HEAD's scene, 3 m/s for 6 s."""
    CR, LF = chr(13) + chr(10), chr(10)
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout.decode('utf-8').replace(CR, LF)
    path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    io.open(path, 'wb').write((head + (LF if not head.endswith(LF) else '') + sp.SPHERE % 6.0).encode('utf-8'))
    env = dict(os.environ, BURST_SCENE=stage_run.HEAD_SCENE, BURST_CAM=s2.CAMS['owner'])
    env.pop('BURST_SLIDE', None)
    try:
        stage_run.restore()
        for shader, text in stage_run.build_variant(variant).items():
            io.open(os.path.join(stage_run.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
        p = subprocess.run([sys.executable, stage_run.BURST, tag, '--speed=0', '--stop=0.1', '--frames=%d' % frames,
                            '--from=%d' % first] + ['--extra=' + f for f in flags],
                           cwd=stage_run.ROOT, capture_output=True, text=True, timeout=3600, errors='replace', env=env)
        got = [f for f in os.listdir(stage_run.SHOTS) if f.startswith(tag + '_')]
        print('%-22s %-10s %d frames %s' % (tag, variant, len(got), ' '.join(flags)))
        if len(got) != frames:
            print(p.stdout[-2000:], p.stderr[-2000:])
            sys.exit('sphere run %s incomplete' % tag)
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


def render(cases):
    for case in cases:
        c = CASES[case]
        if case == 'car':
            moving = dict(BURST_SLIDE='=porsche_992_gt3_r|1.0|2.0')
        elif case == 'cube':
            moving = dict(cube=(-9.0, 3.0, 6.0))
        if case == 'sphere':
            for arm, variant, flags in ARMS:
                sphere_burst('r15i_sphere_%s' % arm, variant, flags, c['k'] - 4, 9)
        else:
            s2.arms_at(c['cam'], [('r15i_%s_%s' % (case, arm), variant, flags,
                                   dict(frames=9, first=c['k'] - 4, **moving)) for arm, variant, flags in ARMS])


def tag_of(case, arm):
    return ('r15i_sphere_%s' % arm) if case == 'sphere' else ('rt5b_r15i_%s_%s' % (case, arm))


def analyse(cases):
    os.makedirs(OUT, exist_ok=True)
    for case in cases:
        c = CASES[case]
        ref = c['ref']()
        refL = L(ref)
        load = lambda tag, f: np.asarray(Image.open(S(tag, f)).convert('RGB'), dtype=float)[:860]
        # HEAD's frames against the build with every item off: the build's own changes, alone.
        worst = 0.0
        for f in range(c['k'] - 4, c['k'] + 5):
            worst = max(worst, np.abs(load(tag_of(case, 'before'), f) - load(c['head'], f)).max())
        errs = {f: np.abs(L(load(tag_of(case, 'before'), f)) - refL).mean() for f in range(c['k'] - 4, c['k'] + 5)}
        best = min(errs, key=errs.get)
        print('%s: settled pose matches frame %d; build with every item off against HEAD, largest pixel difference %.0f'
              % (case, best, worst))
        tiles = []
        for arm, _, _ in ARMS:
            im = Image.open(S(tag_of(case, arm), best)).convert('RGB')
            a = np.asarray(im, dtype=float)[:860]
            g = np.asarray(im.convert('L'), dtype=float)[:860]
            med = np.asarray(im.convert('L').filter(ImageFilter.MedianFilter(3)), dtype=float)[:860]
            cells = []
            for name, (y0, y1, x0, x1) in c['regions'].items():
                e = np.abs(L(a) - refL)[y0:y1, x0:x1]
                cells.append('%s: error %.2f (>16 %.1f%%) grain %.1f%%' % (
                    name, e.mean(), (e > 16).mean() * 100, (np.abs(g - med)[y0:y1, x0:x1] > 16).mean() * 100))
            print('   %-7s %s' % (arm, '   '.join(cells)))
            tiles.append(a)
        y0, y1, x0, x1 = list(c['regions'].values())[0]
        crop = lambda a: a[y0:y1, x0:x1]
        panels = [crop(t) for t in tiles] + [crop(ref)]
        labels = [arm for arm, _, _ in ARMS] + ['settled']
        s2._sheet(os.path.join(OUT, 'items_%s_%d.png' % (case, best)), panels, labels)


if __name__ == '__main__':
    cases = [a for a in sys.argv[1:] if a in CASES] or list(CASES)
    if 'render' in sys.argv:
        render(cases)
    analyse(cases)
