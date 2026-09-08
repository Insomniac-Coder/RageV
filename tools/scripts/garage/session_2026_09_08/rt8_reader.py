"""RT-8: the temporal resolve reads the water layer.

The sea now records its motion; this is what reads it. Where the water layer's
mask says a wave covered the pixel, its motion replaces the one the opaque
geometry behind it wrote -- which for a sea over a seabed is the difference
between reprojecting the wave and reprojecting the ground under it.

Every anchor must match exactly once or nothing is written.
"""
import io, sys

T = '\t'
H = '\t' * 9 + ' '    # TemporalResolve's continuation indent
D = '\t' * 7 + ' '    # Dispatch's


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


patch(r'RageV/src/RageV/Renderer/PostProcess.h', [
 (H + 'bool boxGeometry = true);',
  H + 'bool boxGeometry = true,\n'
  + H + '// **RT-8: the water layer\'s motion and mask.** xy the wave\'s\n'
  + H + '// screen motion in the same units and sign as the scene\'s own\n'
  + H + '// lane, z one where a wave was drawn. Null leaves every pixel\n'
  + H + '// on the geometry\'s velocity, which is what the sea had -- and\n'
  + H + '// what made a moving wave look stationary to this pass.\n'
  + H + 'const RHI::Ref<RHI::RHITexture>& waterMotion = nullptr);'),
 (D + 'const RHI::Ref<RHI::RHITexture>& seventh = nullptr,\n'
  + D + 'Sampling seventhSampling = Sampling::Point);',
  D + 'const RHI::Ref<RHI::RHITexture>& seventh = nullptr,\n'
  + D + 'Sampling seventhSampling = Sampling::Point,\n'
  + D + '// RT-8: binding 9, past the material lane. The water layer.\n'
  + D + 'const RHI::Ref<RHI::RHITexture>& eighth = nullptr,\n'
  + D + 'Sampling eighthSampling = Sampling::Point);'),
], 'waterMotion')

patch(r'RageV/src/RageV/Renderer/PostProcess.cpp', [
 (T*3 + 'const Ref<RHITexture>& seventh, Sampling seventhSampling)\n'
  + T + '{',
  T*3 + 'const Ref<RHITexture>& seventh, Sampling seventhSampling,\n'
  + T*3 + 'const Ref<RHITexture>& eighth, Sampling eighthSampling)\n'
  + T + '{'),
 (T*2 + '// RT-6.2: the material lane, for the resolve alone.\n'
  + T*2 + 'if (seventh)\n'
  + T*3 + 'set->SetTexture(8, seventh, samplerFor(seventhSampling));',
  T*2 + '// RT-6.2: the material lane, for the resolve alone.\n'
  + T*2 + 'if (seventh)\n'
  + T*3 + 'set->SetTexture(8, seventh, samplerFor(seventhSampling));\n'
  + T*2 + '// RT-8: the water layer\'s motion, at 9. The resolve alone again,\n'
  + T*2 + '// and always bound where it is declared -- a mask of zero is what\n'
  + T*2 + '// says "no wave here", so black is a meaningful value rather than\n'
  + T*2 + '// the undefined read an unbound binding would be.\n'
  + T*2 + 'if (eighth)\n'
  + T*3 + 'set->SetTexture(9, eighth, samplerFor(eighthSampling));'),
 (T*3 + 'bool boxGeometry)\n' + T + '{',
  T*3 + 'bool boxGeometry,\n'
  + T*3 + 'const Ref<RHITexture>& waterMotion)\n' + T + '{'),
 (T*3 + 'material, Sampling::Point);',
  T*3 + 'material, Sampling::Point,\n'
  + T*3 + '// RT-8: the water layer. Point for the same reason the scene\'s\n'
  + T*3 + '// velocity is -- and because its z is a mask, which averaged\n'
  + T*3 + '// across a shoreline would be half a wave.\n'
  + T*3 + 'waterMotion ? waterMotion : s_Data->Black, Sampling::Point);'),
], 'waterMotion')

patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
 (T*5 + ' reflectionMotion](RGPassContext& context)',
  T*5 + ' reflectionMotion, waterSurface](RGPassContext& context)'),
 (T*7 + 'boxGeometry);',
  T*7 + 'boxGeometry,\n'
  + T*7 + '// **RT-8: the water layer\'s motion.** Attachment 3 of the sea\'s\n'
  + T*7 + '// own surface pass: where a wave covers a pixel that is the\n'
  + T*7 + '// motion to reproject by, and the geometry lane under it holds\n'
  + T*7 + '// the seabed instead.\n'
  + T*7 + 'waterSurface != kRGInvalid ? context.Color(waterSurface, 3)\n'
  + T*8 + '\t\t\t\t\t\t   : nullptr);'),
], 'waterSurface](RGPassContext')

print('done')
