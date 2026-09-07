"""Engine render at the ground truth's camera, side by side, with numbers.

The camera comes from the .blend itself (groundtruth.py printed it in engine
space), so the two pictures are of the same thing and the difference between
them is the engine's, not the framing's.
"""
import subprocess, os, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
SHOTS = r'C:\Users\ism19\Code\RageV\build\garage_shots'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
# The page's wide still, resized to the render size (the listing's
# 86f1415c73.jpg, kept under tools/scripts/garage/reference/). gt3.py
# renders Cycles at the same solved camera to blender_truth.png, which is
# the noise-free check that the solve is right; the still is the target.
TRUTH = os.path.join(SHOTS, 'still_truth.png')

# The camera behind the page's wide still (86f1415c73.jpg on the CGTrader
# listing), solved by solve_camera.py from the pillar positions in it: glTF
# (-4.05, 2.36, 15.91), yaw 3.24 deg, pitch 1.39 deg down, 44.6 mm on a
# 36 mm sensor = 43.9 deg horizontal. The garage root sits at z = +14 in the
# scene (migrate.py's GARAGE_Z), so the eye does too. --camera takes focus,
# distance, yaw, pitch the way RuntimeLayer unpacks them (Euler (-pitch,
# -yaw, 0), eye = focus - forward * distance); the scene's PerspectiveFOV
# carries the 43.9.
#
# The artist's saved camera is a different shot (the listing's 5a886a248a:
# x = -8.45, yawed 10.9 deg, 50 mm); gt2.py still renders that one to
# blender_truth_artistcam.png. The earlier '0,1.8,-12,26,0,3' matched
# neither -- it put the eye 26 m nearer the wall and dead centre.
import math
_GARAGE_Z = 14.0
_POS = (-4.05, 2.36, 15.91 + _GARAGE_Z)
_YAW = 3.24
_PITCH = 1.39
_FWD = (math.sin(math.radians(_YAW)) * math.cos(math.radians(_PITCH)),
        -math.sin(math.radians(_PITCH)),
        -math.cos(math.radians(_YAW)) * math.cos(math.radians(_PITCH)))
_D = 20.0
_FOCUS = tuple(p + f * _D for p, f in zip(_POS, _FWD))
CAM = '%.4f,%.4f,%.4f,%.1f,%.3f,%.3f' % (_FOCUS + (_D, _YAW, _PITCH))
FOV = 43.9

# The car shot (owner, 2026-09-05 night: "the camera is a bit closer to the
# car, right now it's too far"): the same axis as the still, the eye twelve
# metres from the car at (-2.3, 0, -2) instead of thirty-two, a touch lower.
# No truth to compare against; `--car` renders it and prints nothing.
_CAR = (-2.3, 0.0, -2.0)


def car_camera(distance, height=1.4):
    pos = (_CAR[0] - _FWD[0] * distance, height, _CAR[2] - _FWD[2] * distance)
    focus = tuple(p + f * _D for p, f in zip(pos, _FWD))
    return '%.4f,%.4f,%.4f,%.1f,%.3f,%.3f' % (focus + (_D, _YAW, _PITCH))


CAM_CAR = car_camera(12.0)


def render(name, extra=(), cam=None, size=(1280, 720)):
    out = os.path.join(SHOTS, name + '.png')
    if os.path.exists(out):
        os.remove(out)
    cmd = [os.path.join(RT, 'RageVRuntime.exe'),
           '--project=' + os.path.join(ROOT, 'SampleProject'),
           '--scene=scenes/showroom.rage', '--rhi=vulkan',
           '--render-defaults=off', '--vsync=off',
           '--width=%d' % size[0], '--height=%d' % size[1],
           '--screenshot=' + out, '--screenshot-frame=60',
           '--import-cache=off', '--frame-time=0.000001',
           '--camera=' + (cam or CAM)] + list(extra)
    p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True,
                       timeout=1200, errors='replace')
    if not os.path.exists(out):
        print(p.stdout[-1200:])
        raise SystemExit('no render')
    return out


def compare(mine):
    A = np.asarray(Image.open(TRUTH).convert('RGB'), dtype=float)
    B = np.asarray(Image.open(mine).convert('RGB'), dtype=float)
    if A.shape != B.shape:
        B = np.asarray(Image.open(mine).convert('RGB').resize(
            (A.shape[1], A.shape[0])), dtype=float)
    d = np.abs(A - B)
    print('  mean abs difference : %.1f levels' % d.mean())
    print('  truth  mean RGB     : %.1f %.1f %.1f' % tuple(A.reshape(-1, 3).mean(0)))
    print('  engine mean RGB     : %.1f %.1f %.1f' % tuple(B.reshape(-1, 3).mean(0)))
    # where the biggest disagreements are, by band of the frame
    h = A.shape[0]
    for label, lo, hi in (('top third (ceiling)', 0, h // 3),
                          ('middle (walls)', h // 3, 2 * h // 3),
                          ('bottom third (floor)', 2 * h // 3, h)):
        print('  %-22s truth %5.1f   engine %5.1f   diff %5.1f'
              % (label, A[lo:hi].mean(), B[lo:hi].mean(), d[lo:hi].mean()))
    # a side-by-side for looking at
    side = Image.new('RGB', (A.shape[1], A.shape[0] * 2))
    side.paste(Image.open(TRUTH).convert('RGB'), (0, 0))
    side.paste(Image.open(mine).convert('RGB').resize((A.shape[1], A.shape[0])),
               (0, A.shape[0]))
    sp = os.path.join(SHOTS, 'side_by_side.png')
    side.save(sp)
    print('  side by side ->', sp)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    name = args[0] if args else 'engine_at_truth'
    car = next((a for a in sys.argv[1:] if a.startswith('--car')), None)
    if car:
        # --car=<metres>[,<width>x<height>]
        spec = car.split('=', 1)[1] if '=' in car else '12'
        metres, _, wh = spec.partition(',')
        size = tuple(int(v) for v in wh.split('x')) if wh else (1280, 720)
        print('  car shot ->', render(name, cam=car_camera(float(metres)), size=size))
    else:
        compare(render(name))
