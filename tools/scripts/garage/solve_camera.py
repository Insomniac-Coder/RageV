"""Solve the camera that made the page's wide still (86f1415c73.jpg) from
pillar positions measured in it. glTF axes (x right, y up, camera looks
down -z at yaw 0); the garage root adds z = +14 in the scene.

Measurements (image 1800x1014, principal point at the centre): the far
pillar pair's centres and their floor/ceiling ends, the second pair's
centres, the near-left pillar's right edge. See the session notes for how
each was read (vertical-edge peaks in rows 380-580, row-mean steps).
"""
import numpy as np, math

W, H = 1800, 1014
PP = np.array([W / 2, H / 2])
PILLAR_R = 0.94   # x half-width of a column (node scale x)

# (u, v or None), (x, y, z)
OBS = [
    # far pillar pair: centre at the floor (the wall/floor step reads at
    # y = 602 in the still)
    ((405, 600), (-11.06, 0.0, -25.76)),
    ((1555, 600), (10.87, 0.0, -25.76)),
    # second pair and the near-left pillar's right edge: horizontal only
    ((270, None), (-11.06, 3.0, -17.13)),
    ((1761, None), (10.87, 3.0, -17.13)),
    ((185, None), (-11.06 + PILLAR_R, 3.0, -7.28)),
    # the left tube group's five rows (x -10.18..-7.1, y 4.4): bright rows
    # at 310, 329, 344, 357, 368 for z = -23.84, -19.97, -16.11, -12.24,
    # -8.41 -- vertical only, a height and pitch cue that the pillars are not
    ((None, 310), (-8.64, 4.4, -23.84)),
    ((None, 329), (-8.64, 4.4, -19.97)),
    ((None, 344), (-8.64, 4.4, -16.11)),
    ((None, 357), (-8.64, 4.4, -12.24)),
    ((None, 368), (-8.64, 4.4, -8.41)),
]


def project(p, cam):
    cx, cy, cz, yaw, pitch, f = cam
    fwd = np.array([math.sin(yaw) * math.cos(pitch), -math.sin(pitch), -math.cos(yaw) * math.cos(pitch)])
    right = np.array([math.cos(yaw), 0.0, math.sin(yaw)])
    up = np.cross(right, fwd)
    d = np.array(p) - np.array([cx, cy, cz])
    zc = d @ fwd
    return PP[0] + f * (d @ right) / zc, PP[1] - f * (d @ up) / zc


def residuals(cam):
    r = []
    for (u, v), p in OBS:
        pu, pv = project(p, cam)
        if u is not None:
            r.append(pu - u)
        if v is not None:
            r.append(pv - v)
    return np.array(r)


cam = np.array([-1.5, 2.0, 11.0, 0.0, 0.05, 1900.0])
for it in range(60):
    r = residuals(cam)
    J = np.zeros((len(r), 6))
    for k in range(6):
        h = 1e-4 * max(1.0, abs(cam[k]))
        c2 = cam.copy(); c2[k] += h
        J[:, k] = (residuals(c2) - r) / h
    step = np.linalg.lstsq(J.T @ J + 1e-6 * np.eye(6), -J.T @ r, rcond=None)[0]
    cam = cam + step
    if np.abs(step).max() < 1e-7:
        break
r = residuals(cam)
cx, cy, cz, yaw, pitch, f = cam
print('camera glTF pos = (%.2f, %.2f, %.2f)  yaw %.2f deg  pitch %.2f deg  f %.0f px' % (cx, cy, cz, math.degrees(yaw), math.degrees(pitch), f))
print('horizontal fov %.1f deg  = %.1f mm on a 36 mm sensor' % (math.degrees(2 * math.atan(W / 2 / f)), 18 / math.tan(math.atan(W / 2 / f))))
print('rms residual %.1f px, worst %.1f px' % (math.sqrt((r ** 2).mean()), np.abs(r).max()))
for (u, v), p in OBS:
    pu, pv = project(p, cam)
    print('   %-28s image (%4s, %4s)  model (%6.1f, %6.1f)' % (p, u, v, pu, pv))
print('world (scene) pos = (%.2f, %.2f, %.2f)' % (cx, cy, cz + 14.0))
fwd = (math.sin(yaw) * math.cos(pitch), -math.sin(pitch), -math.cos(yaw) * math.cos(pitch))
print('forward = (%.4f, %.4f, %.4f)' % fwd)
