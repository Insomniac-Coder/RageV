import bpy, os, sys
OUTDIR = r'C:\Users\ism19\Code\RageV\SampleProject\assets\models\garage_gltf'
os.makedirs(OUTDIR, exist_ok=True)
out = os.path.join(OUTDIR, 'underground_garage.gltf')
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.ops.export_scene.gltf(filepath=out, export_format='GLTF_SEPARATE',
                          use_selection=True, export_yup=True,
                          export_materials='EXPORT', export_image_format='AUTO',
                          export_cameras=False, export_lights=False,
                          export_animations=False)
n = len([f for f in os.listdir(OUTDIR)])
print('WROTE %s  (%d files in folder)' % (out, n))
