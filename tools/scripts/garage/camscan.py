"""Find the camera behind the page's wide still by silhouette matching.

Workbench renders of just the columns, the shell, the floor and the ceiling
slabs in flat colours, at candidate poses, scored against edges measured in
the still (86f1415c73.jpg): the pillar silhouettes in rows 380-580, the
wall/floor step, the wall's top. Coordinate descent from solve_camera.py's
answer. Prints the best pose in glTF axes and the scene's (+14 z).
"""
import bpy, os, math
import numpy as np
from mathutils import Vector, Matrix

OUT = r'C:\Users\ism19\Code\RageV\build\garage_shots\camscan'
os.makedirs(OUT, exist_ok=True)
W, H = 900, 507                       # half the still
STILL_EDGES = [x / 2 for x in (185, 354, 452, 1511, 1680)]
STILL_FLOOR = 602 / 2
STILL_TOP = 220 / 2

keep = {'Cube': (0.3, 0.3, 0.3, 1), 'Plane.016': (0.6, 0.6, 0.6, 1),
        'Plane.003': (0.15, 0.15, 0.15, 1), 'Plane.010': (0.15, 0.15, 0.15, 1)}
for i in range(13, 23):
    keep['Cylinder.%03d' % i] = (1, 1, 1, 1)
for o in bpy.data.objects:
    if o.type != 'MESH':
        continue
    o.hide_render = o.name not in keep
    if o.name in keep:
        o.color = keep[o.name]

sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'FLAT'
sc.display.shading.color_type = 'OBJECT'
sc.display.render_aa = 'OFF'
sc.render.resolution_x, sc.render.resolution_y = W, H
sc.render.resolution_percentage = 100
# the file's output format is locked to FFMPEG (HANDOFF): render, then save_render
sc.render.film_transparent = False
cam = sc.camera
cam.data.sensor_width = 36.0
cam.data.sensor_fit = 'HORIZONTAL'


def pose(x, h, z, yaw, pitch, f):
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    fwd_gl = (math.sin(yaw) * math.cos(pitch), -math.sin(pitch), -math.cos(yaw) * math.cos(pitch))
    loc = Vector((x, -z, h))
    fwd = Vector((fwd_gl[0], -fwd_gl[2], fwd_gl[1]))
    cam.matrix_world = Matrix.Translation(loc) @ fwd.to_track_quat('-Z', 'Z').to_matrix().to_4x4()
    cam.data.lens = 18.0 / (900.0 / f)      # f px = 900 px * lens / 18 mm


n = [0]
def render(p):
    pose(*p)
    path = os.path.join(OUT, 'scan.png')
    bpy.ops.render.render(write_still=False)
    bpy.data.images['Render Result'].save_render(filepath=path)
    img = bpy.data.images.load(path)
    buf = np.empty(W * H * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    a = buf.reshape(H, W, 4)[::-1, :, 0]     # top row first, red channel
    n[0] += 1
    return a


def score(p):
    a = render(p)
    band = a[190:290]
    gx = np.abs(np.diff(band, axis=1)).mean(0)
    edges = [x for x in range(2, W - 3) if gx[x] == gx[x-2:x+3].max() and gx[x] > 0.05]
    s = 0.0
    for e in STILL_EDGES:
        s += min(abs(e - x) for x in edges) if edges else 100.0
    col = a[:, 300:725].mean(1)
    # the wall (0.3) sits between the ceiling (0.15) above and the floor (0.6) below
    floor = next((y for y in range(150, H - 1) if col[y] < 0.45 and col[y + 1] >= 0.45), H)
    top = next((y for y in range(0, H - 1) if col[y] < 0.22 and col[y + 1] >= 0.22), 0)
    s += abs(floor - STILL_FLOOR) + abs(top - STILL_TOP)
    return s, edges, floor, top


best = [-4.05, 2.36, 15.91, 3.24, 1.39, 2230.0]
steps = [0.5, 0.4, 1.5, 0.6, 0.6, 120.0]
cur, edges, floor, top = score(best)
print('start score %.1f edges=%s floor=%d top=%d' % (cur, edges, floor, top), flush=True)
for rnd in range(4):
    improved = False
    for k in range(6):
        for sgn in (-1, 1):
            trial = list(best)
            trial[k] += sgn * steps[k]
            s, e, fl, tp = score(trial)
            if s < cur - 1e-6:
                best, cur, improved = trial, s, True
                print('  round %d param %d -> %s score %.1f (edges %s floor %d top %d)'
                      % (rnd, k, [round(v, 2) for v in best], cur, e, fl, tp), flush=True)
    if not improved:
        steps = [st * 0.5 for st in steps]
x, h, z, yaw, pitch, f = best
print('BEST glTF (%.2f, %.2f, %.2f) yaw %.2f pitch %.2f f %.0f -> fov %.1f deg, lens %.1f mm; scene z %.2f; score %.1f after %d renders'
      % (x, h, z, yaw, pitch, f, math.degrees(2 * math.atan(900 / f)), 18 * f / 900, z + 14, cur, n[0]), flush=True)
render(best)
os.replace(os.path.join(OUT, 'scan.png'), os.path.join(OUT, 'best.png'))
