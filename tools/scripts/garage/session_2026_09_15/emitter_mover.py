# -*- coding: utf-8 -*-
"""A moving emitter that is also a source of light (owner, 2026-09-15): what does the scene do?

HEAD's garage with two entities added, driven together along world X by the engine's
Slider (1.5 m/s from x = -8 for 6 s, at z = -6, across the owner's shot):
  GlowingMover -- a 0.4 m cube, emissive [8, 5, 2.5]: above the emitter list's threshold, so
                  the list aims shadow rays at it, and every ray that sees it sees it glow
  MoverLight   -- a Realtime point light 0.45 m under it (outside the cube, so the cube does
                  not block its own shadow rays), warm, intensity 25, range 10, casting shadows

  run        the drive, frames 60-129 (the object moving throughout), the owner's shot
  ref        the same drive stopped where frame 90 has it, settled: the mean of frames 150-169
  log        one run with the log kept, for what the bake and the emitter list say about it
  analyse    frames, the moving frame against the settled pose, and where the error sits

Usage: emitter_mover.py run|ref|log|analyse
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_14'))
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import stage_run  # noqa: E402
import rt13_stage2 as s2  # noqa: E402

CR, LF = chr(13) + chr(10), chr(10)
BURST = stage_run.BURST
OUT = os.path.join(stage_run.ROOT, 'build', 'emitter')
FT = 0.0166
K = 90
START, SPEED, STOP = -8.0, 1.5, 6.0

ENTITIES = '''  - EntityID: 7311000000000000201
    TagComponent:
      Tag: GlowingMover
    TransformComponent:
      Position: [%(x)g, 1.2, -6]
      Rotation: [0, 0, 0]
      Scale: [0.4, 0.4, 0.4]
    MeshComponent:
      Static: false
      Mesh: 8241982477996916736
      Material:
        BaseColor: [1, 1, 1, 1]
        Emissive: [8, 5, 2.5, 1]
        Metallic: 0
        Roughness: 0.5
        Occlusion: 1
    NativeScriptComponent:
      Script: Slider
      Fields:
        Speed: %(speed)g
        StopAfter: %(stop)g
  - EntityID: 7311000000000000202
    TagComponent:
      Tag: MoverLight
    TransformComponent:
      Position: [%(x)g, 0.75, -6]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    LightComponent:
      Type: Point
      Color: [1, 0.62, 0.3]
      Intensity: 25
      Range: 10
      InnerCone: 20
      OuterCone: 30
      CastShadows: true
      Mobility: Realtime
    NativeScriptComponent:
      Script: Slider
      Fields:
        Speed: %(speed)g
        StopAfter: %(stop)g
'''


def scene(stop):
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout.decode('utf-8').replace(CR, LF)
    if not head.endswith(LF):
        head += LF
    return (head + ENTITIES % dict(x=START, speed=SPEED, stop=stop)).encode('utf-8')


def burst(tag, stop, first, frames, extra=(), keep_log=False):
    path = os.path.join(stage_run.SCENES, stage_run.HEAD_SCENE)
    io.open(path, 'wb').write(scene(stop))
    env = dict(os.environ, BURST_SCENE=stage_run.HEAD_SCENE, BURST_CAM=s2.CAMS['owner'])
    env.pop('BURST_SLIDE', None)
    env.pop('BURST_SWITCH', None)
    cmd = [sys.executable, BURST, tag, '--speed=0', '--stop=0.1', '--frames=%d' % frames, '--from=%d' % first]
    cmd += ['--extra=' + f for f in extra]
    try:
        stage_run.restore()
        p = subprocess.run(cmd, cwd=stage_run.ROOT, capture_output=True, text=True, timeout=3600, errors='replace', env=env)
        if keep_log:
            os.makedirs(OUT, exist_ok=True)
            io.open(os.path.join(OUT, tag + '.log'), 'w', encoding='utf-8').write(p.stdout + p.stderr)
        got = [f for f in os.listdir(stage_run.SHOTS) if re.match(re.escape(tag) + r'_\d+\.png$', f)]
        print('%-16s %d frames' % (tag, len(got)))
        return p
    finally:
        for f in (stage_run.HEAD_SCENE, stage_run.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(stage_run.SCENES, f))
            except OSError:
                pass


def lum(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def img(tag, k):
    return np.asarray(Image.open(os.path.join(stage_run.SHOTS, '%s_%d.png' % (tag, k))).convert('RGB'), dtype=float)[:860]


if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'run':
        burst('em_drive', STOP, 60, 70)
    elif what == 'ref':
        burst('em_ref%d' % K, K * FT, 150, 20)
    elif what == 'log':
        burst('em_log', STOP, 60, 2, keep_log=True)
    elif what == 'analyse':
        os.makedirs(OUT, exist_ok=True)
        ref = np.mean([img('em_ref%d' % K, k) for k in range(150, 170)], axis=0)
        errs = {k: np.abs(lum(img('em_drive', k)) - lum(ref)).mean() for k in range(K - 3, K + 4)}
        best = min(errs, key=errs.get)
        print('the stopped object matches moving frame %d (%s)' % (best, ', '.join('%d:%.2f' % kv for kv in sorted(errs.items()))))
        mov = img('em_drive', best)
        d = lum(mov) - lum(ref)
        heat = np.zeros_like(mov)
        heat[..., 0] = np.clip(d * 4.0, 0, 255)
        heat[..., 2] = np.clip(-d * 4.0, 0, 255)
        s2._sheet(os.path.join(OUT, 'drive_vs_settled_%d.png' % best), [mov, ref, heat],
                  ['moving, frame %d' % best, 'stopped there, settled', 'red: moving brighter, blue: darker (x4)'])
        for name, (y0, y1, x0, x1) in {'whole': (0, 860, 0, 1600), 'floor': (560, 860, 0, 1600),
                                       'wall behind': (100, 460, 400, 1500)}.items():
            dd = np.abs(d[y0:y1, x0:x1])
            print('   %-12s mean |moving - settled| %.2f   px > 8: %.2f%%   px > 24: %.2f%%'
                  % (name, dd.mean(), (dd > 8).mean() * 100, (dd > 24).mean() * 100))
        tiles = [img('em_drive', k) for k in (62, 80, 100, 120)]
        top = np.concatenate(tiles[:2], axis=1)
        bot = np.concatenate(tiles[2:], axis=1)
        Image.fromarray(np.concatenate([top, bot], axis=0).astype(np.uint8)).resize((1600, 860)).save(os.path.join(OUT, 'drive_4frames.png'))
