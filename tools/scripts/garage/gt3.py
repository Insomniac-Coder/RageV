"""Cycles at the camera solve_camera.py found for the page's wide still.

    blender --background --factory-startup <blend> --python tools/scripts/garage/gt3.py

-> build/garage_shots/blender_truth.png (1280x720). If the solve is right this
matches reference/cgtrader_86f1415c73_wide.jpg to within the measurement
error; if it is not, the pillars land elsewhere and say so.
"""
import bpy, os, math
from mathutils import Vector, Matrix

OUT = r'C:\Users\ism19\Code\RageV\build\garage_shots\blender_truth.png'
# glTF axes -> Blender: (x, y, z) -> (x, -z, y)
POS_GL = (-4.05, 2.36, 15.91)
YAW, PITCH = math.radians(3.24), math.radians(1.39)
FWD_GL = (math.sin(YAW) * math.cos(PITCH), -math.sin(PITCH), -math.cos(YAW) * math.cos(PITCH))
LENS = 44.6

sc = bpy.context.scene
cam = sc.camera
loc = Vector((POS_GL[0], -POS_GL[2], POS_GL[1]))
fwd = Vector((FWD_GL[0], -FWD_GL[2], FWD_GL[1]))
rot = fwd.to_track_quat('-Z', 'Z')
cam.matrix_world = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
cam.data.lens = LENS
cam.data.sensor_width = 36.0
cam.data.sensor_fit = 'HORIZONTAL'
print('camera at', tuple(round(c, 3) for c in loc), 'forward', tuple(round(c, 4) for c in fwd), 'lens', LENS)

sc.render.engine = 'CYCLES'
sc.cycles.samples = 64
sc.cycles.use_denoising = True
sc.render.resolution_x = 1280
sc.render.resolution_y = 720
sc.render.resolution_percentage = 100
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.get_devices()
    prefs.compute_device_type = 'OPTIX'
    for d in prefs.devices:
        d.use = True
    sc.cycles.device = 'GPU'
except Exception as e:
    print('gpu', e)
bpy.ops.render.render(write_still=False)
bpy.data.images['Render Result'].save_render(filepath=OUT)
print('RENDERED', OUT, os.path.getsize(OUT))
