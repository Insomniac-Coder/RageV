# -*- coding: utf-8 -*-
"""Which signal holds the moving light's old light? (emitter_mover.py's drive, the close-up)

Each arm renders the drive (frames 95-107) and its own settled pose (the drive stopped where
frame 100 has it, frames 150-169), so each arm is compared with the picture it would settle to:
  ship       as shipped
  nodirect   --direct-signal=off   the lamp light walked in the lit shader, no history
  nogi       --gi-signal=off       the bounce from last frame's buffer, not the signal
  norefl     --rt-reflections=off  the probe answers every reflection
  nochange   --measured-change=off the histories without measured change

Usage: emitter_lag.py [render]
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import emitter_mover as em  # noqa: E402

stage_run, s2 = em.stage_run, em.s2
K = 100
ARMS = [('ship', []), ('nodirect', ['--direct-signal=off']), ('nogi', ['--gi-signal=off']),
        ('norefl', ['--rt-reflections=off']), ('nochange', ['--measured-change=off'])]
REGIONS = {'floor left of car': (380, 860, 0, 250), 'car body (side)': (380, 760, 250, 900),
           'rear side window': (280, 420, 250, 450)}


def burst(tag, stop, first, frames, flags):
    path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    io.open(path, 'wb').write(em.scene(stop))
    env = dict(os.environ, BURST_SCENE=stage_run.HEAD_SCENE, BURST_CAM=s2.CAMS['close'])
    env.pop('BURST_SLIDE', None)
    try:
        stage_run.restore()
        cmd = [sys.executable, stage_run.BURST, tag, '--speed=0', '--stop=0.1', '--frames=%d' % frames, '--from=%d' % first]
        cmd += ['--extra=' + f for f in flags]
        subprocess.run(cmd, cwd=stage_run.ROOT, capture_output=True, text=True, timeout=3600, errors='replace', env=env)
    finally:
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


img = lambda tag, k: np.asarray(Image.open(os.path.join(stage_run.SHOTS, '%s_%d.png' % (tag, k))).convert('RGB'), dtype=float)[:860]
lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]

if __name__ == '__main__':
    if 'render' in sys.argv:
        for arm, flags in ARMS:
            burst('eml_%s' % arm, em.STOP, 95, 13, flags)
            burst('eml_%s_ref' % arm, K * em.FT, 150, 20, flags)
            print(arm, 'rendered')
            sys.stdout.flush()
    tiles, labels = [], []
    for arm, flags in ARMS:
        ref = np.mean([img('eml_%s_ref' % arm, k) for k in range(150, 170)], axis=0)
        mov = img('eml_%s' % arm, K + 1)   # the stopped pose matches frame K + 1 (emitter_mover's check)
        d = lum(mov) - lum(ref)
        cells = []
        for name, (y0, y1, x0, x1) in REGIONS.items():
            dd = d[y0:y1, x0:x1]
            cells.append('%s %.2f (>16: %.1f%%)' % (name, np.abs(dd).mean(), (np.abs(dd) > 16).mean() * 100))
        print('%-9s lag vs its own settled pose:  %s' % (arm, '   '.join(cells)))
        heat = np.zeros_like(mov)
        heat[..., 0] = np.clip(d * 4, 0, 255)
        heat[..., 2] = np.clip(-d * 4, 0, 255)
        tiles.append(heat[150:860, 0:1000])
        labels.append(arm + ' ' + ' '.join(flags))
    s2._sheet(os.path.join(em.OUT, 'lag_by_signal.png'), tiles, labels)
