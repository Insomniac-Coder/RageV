import bpy, os
OUT = r'C:\Users\ism19\Code\RageV\build\garage_shots\blender_truth.png'
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
try:
    sc.cycles.samples = 64
    sc.cycles.use_denoising = True
except Exception as e:
    print('cycles cfg', e)
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
img = bpy.data.images.get('Render Result')
img.save_render(filepath=OUT)
print('RENDERED', OUT, os.path.getsize(OUT))
