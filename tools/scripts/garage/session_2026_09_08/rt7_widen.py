"""RT-7: the length gets its own lane, because the tubes are spot lights.

The first encoding put the length in `Params.z`, which is free on everything
but a spot -- and the garage's tubes *are* spots (pointing down, an 88 degree
outer cone), so the lane it was free in is the one case it had to serve. A
sixth vec4 instead: sixteen more bytes on an eighty-byte record, which is the
cost the doc sanctions ("find the next spare lane or widen deliberately"), and
it is where WR-8's area shape goes next rather than a second widening later.

Both GLSL mirrors and the static_assert have to move together or the buffer
reads garbage.
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


# --------------------------------------------------------- the C++ record
patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
 (T*3 + 'Vec4 Shadow;      // kind, slot (under rays: a moving object is in range, 7cx), far, texel scale\n'
  + T*2 + '};\n'
  + T*2 + 'static_assert(sizeof(GpuLight) == 80, "Must match GpuLight in pbr.rvshader");',
  T*3 + 'Vec4 Shadow;      // kind, slot (under rays: a moving object is in range, 7cx), far, texel scale\n'
  + T*3 + '// **RT-7: the source\'s extent.** x is the length in metres along\n'
  + T*3 + '// Direction (Light::SourceLength); y, z and w are spare and are\n'
  + T*3 + '// where WR-8\'s area shape goes -- a rectangle needs two axes and a\n'
  + T*3 + '// kind, which is exactly what is left here.\n'
  + T*3 + '//\n'
  + T*3 + '// A lane of its own rather than a free corner of another: the\n'
  + T*3 + '// obvious free corner was Params.z, which holds cos(outer) on a\n'
  + T*3 + '// spot and nothing on anything else -- and the tubes this exists\n'
  + T*3 + '// for are spots, so the one encoding that cost no bytes could not\n'
  + T*3 + '// serve the one case that needed it.\n'
  + T*3 + 'Vec4 Extent;\n'
  + T*2 + '};\n'
  + T*2 + 'static_assert(sizeof(GpuLight) == 96, "Must match GpuLight in pbr.rvshader");'),
 # The packing: Params.z goes back to what it was, and the length rides Extent.
 (T*3 + '// **RT-7: and the outer lane carries the source length when there\n'
  + T*3 + '// is no cone to carry.** A cosine cannot exceed 1, so 1 plus the\n'
  + T*3 + '// length is unambiguous, and the shader\'s cone test\n'
  + T*3 + '// (`Params.z < Params.y`) stays false for any length, as it was\n'
  + T*3 + '// with the 1.0 this replaces. Zero length writes exactly 1.0, so\n'
  + T*3 + '// every light authored before this packs to the bit it always did.\n'
  + T*3 + 'const float outer = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.OuterCone))\n'
  + T*4 + '\t\t\t  : 1.0f + Math::Max(light.SourceLength, 0.0f);',
  T*3 + 'const float outer = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.OuterCone)) : 1.0f;'),
])

src = io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', encoding='utf-8', newline='').read()
if 'entry.Extent' not in src:
    s = src.replace('\r\n', '\n')
    anchor = T*3 + 'entry.Params = { Math::Max(light.Range, 0.0001f), inner, outer, mobility };'
    if s.count(anchor) != 1:
        sys.exit('Params write matched %d times' % s.count(anchor))
    s = s.replace(anchor, anchor + '\n'
                  + T*3 + '// RT-7: metres of source length along Direction; zero is the\n'
                  + T*3 + '// sphere every light was before it.\n'
                  + T*3 + 'entry.Extent = { Math::Max(light.SourceLength, 0.0f), 0.0f, 0.0f, 0.0f };', 1)
    io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', 'w', encoding='utf-8',
            newline='\r\n' if '\r\n' in src else '\n').write(s)
    print('patched the Extent write')

# ------------------------------------------------------------ both mirrors
patch(r'RageVEditor/assets/shaders/include/pbr_fragment.glsl', [
 ('\t// x kind of shadow map (0 none), y slot, z far distance, w texel scale\n'
  '\tvec4 Shadow;\n'
  '};',
  '\t// x kind of shadow map (0 none), y slot, z far distance, w texel scale\n'
  '\tvec4 Shadow;\n'
  '\t// **RT-7: the source\'s extent.** x the length in metres along\n'
  '\t// Direction (Light::SourceLength), zero for a sphere; y, z and w spare,\n'
  '\t// and where WR-8\'s area shape goes. A lane of its own because the\n'
  '\t// obvious free corner -- Params.z, a cosine only on a spot -- is taken\n'
  '\t// on exactly the lights this exists for.\n'
  '\tvec4 Extent;\n'
  '};'),
 (T*2 + 'const float tubeLength = isPositional != 0.0\n'
  + T*3 + '\t\t\t\t\t   ? max(light.Params.z - 1.0, 0.0) : 0.0;',
  T*2 + 'const float tubeLength = isPositional != 0.0 ? max(light.Extent.x, 0.0) : 0.0;'),
])

patch(r'RageVEditor/assets/shaders/light_glow.rvshader', [
 ('\tvec4 Params;      // x range, y cos(inner), z cos(outer), w IsBaked\n'
  '\tvec4 Shadow;\n'
  '};',
  '\tvec4 Params;      // x range, y cos(inner), z cos(outer), w IsBaked\n'
  '\tvec4 Shadow;\n'
  '\t// RT-7: x the source length in metres, the rest spare. Unread here --\n'
  '\t// declared because this buffer is the same one pbr_fragment reads and\n'
  '\t// a mirror that disagrees on the stride reads the wrong light.\n'
  '\tvec4 Extent;\n'
  '};'),
])

print('done')
