"""RT-7: the tube's axis is its own, not the direction it aims.

A ceiling tube is a spot: it points *down*, and it is long across the bay. So
the light's forward vector is the aim and says nothing about where the tube
runs. The length lies along the fixture's own local X, which the transform
already knows -- so the axis is derived, not authored, and a tube stays one
number to set.
"""
import io, sys

T = '\t'


def patch(path, edits, marker=None):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker and marker in s:
        print('%s already patched, skipped' % path)
        return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


patch(r'RageV/src/RageV/Renderer/Light.h', [
 (T*2 + 'float SourceLength = 0.0f;\n'
  '\n'
  + T*2 + '// Only one directional light\'s shadows are rendered per frame',
  T*2 + 'float SourceLength = 0.0f;\n'
  '\n'
  + T*2 + '// Only one directional light\'s shadows are rendered per frame'),
 (T*2 + '// See Light::SourceLength. Rides GpuLight.Extent.x, a lane of its\n'
  + T*2 + '// own: the free corner it first used (Params.z, a cosine only on a\n'
  + T*2 + '// spot) is taken on exactly the lights this exists for -- the\n'
  + T*2 + '// garage\'s tubes are spots.\n'
  + T*2 + 'float SourceLength = 0.0f;',
  T*2 + '// See Light::SourceLength. Rides GpuLight.Extent.x, a lane of its\n'
  + T*2 + '// own: the free corner it first used (Params.z, a cosine only on a\n'
  + T*2 + '// spot) is taken on exactly the lights this exists for -- the\n'
  + T*2 + '// garage\'s tubes are spots.\n'
  + T*2 + 'float SourceLength = 0.0f;\n'
  + T*2 + '// **Which way the source runs**, world space and unit length, in\n'
  + T*2 + '// GpuLight.Extent.yzw. Not authored and not `Direction`: a ceiling\n'
  + T*2 + '// tube aims down and is long across the bay, so the direction it\n'
  + T*2 + '// throws light says nothing about where the tube lies. Scene fills\n'
  + T*2 + '// it from the fixture\'s own local X, which is what rotating the\n'
  + T*2 + '// fitting already turns, so a tube stays one number to author.\n'
  + T*2 + 'Vec3 Axis{ 1.0f, 0.0f, 0.0f };'),
])

patch(r'RageV/src/RageV/Scene/Scene.cpp', [
 (T*3 + 'data.SourceLength = light.Light.SourceLength;',
  T*3 + 'data.SourceLength = light.Light.SourceLength;\n'
  + T*3 + '// RT-7: the fixture\'s local X in world space -- the way a tube\n'
  + T*3 + '// runs. Derived here for the same reason Direction is: the\n'
  + T*3 + '// renderer never sees a transform.\n'
  + T*3 + 'data.Axis = Math::Normalize(\n'
  + T*4 + 'Vec3(transform.World * Vec4(1.0f, 0.0f, 0.0f, 0.0f)));'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
 (T*3 + 'entry.Extent = { Math::Max(light.SourceLength, 0.0f), 0.0f, 0.0f, 0.0f };',
  T*3 + 'entry.Extent = { Math::Max(light.SourceLength, 0.0f),\n'
  + T*4 + '\t\t\t light.Axis.x, light.Axis.y, light.Axis.z };'),
])

patch(r'RageVEditor/assets/shaders/include/pbr_fragment.glsl', [
 ('\t// **RT-7: the source\'s extent.** x the length in metres along\n'
  '\t// Direction (Light::SourceLength), zero for a sphere; y, z and w spare,\n'
  '\t// and where WR-8\'s area shape goes. A lane of its own because the\n'
  '\t// obvious free corner -- Params.z, a cosine only on a spot -- is taken\n'
  '\t// on exactly the lights this exists for.\n'
  '\tvec4 Extent;',
  '\t// **RT-7: the source\'s extent.** x the length in metres, zero for a\n'
  '\t// sphere; yzw the unit axis it runs along -- the fixture\'s own, not the\n'
  '\t// direction it aims, because a ceiling tube points down and lies across\n'
  '\t// the bay. A lane of its own because the obvious free corner --\n'
  '\t// Params.z, a cosine only on a spot -- is taken on exactly the lights\n'
  '\t// this exists for.\n'
  '\tvec4 Extent;'),
 (T*4 + 'const vec3 halfAxis = light.Direction.xyz * (0.5 * tubeLength);',
  T*4 + 'const vec3 halfAxis = light.Extent.yzw * (0.5 * tubeLength);'),
])

print('done')
